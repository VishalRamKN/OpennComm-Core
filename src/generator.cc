/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 OpennComm contributors
 */
#include "generator.h"

#include <QElapsedTimer>
#include <QThread>
#include <QByteArray>
#include <QFileInfo>

#include <string>
#include <vector>

#include "llama.h"

namespace {

/* Constrain the CHARACTER SET of the output in the decoder.
 *
 * This is a reduced version of what was intended. The plan was a structured
 * grammar forcing exactly four lines. It does not survive contact with
 * llama.cpp v0.4.1: any grammar built from sub-rules and sequences eventually
 * aborts the whole process with
 *
 *     Unexpected empty grammar stack after accepting piece: \n (198)
 *
 * once a sampled token completes a rule. Reproduced with
 * `root ::= line "\n" line "\n" line "\n" line "\n"`, with an added
 * always-continuable tail, and under greedy sampling with no other samplers in
 * the chain -- so it is neither the tail nor the sampler order. A single flat
 * repetition, as below, is stable.
 *
 * What is lost is only an optimisation. Line count is not a safety property
 * here: oc_options_from_model() drops unusable lines and tops the shortfall up
 * from the phrasebook, so four good buttons are guaranteed whatever the model
 * emits. See the tests in core/tests/test_answers.c.
 *
 * What is kept is worth keeping on its own: no digits and no punctuation. These
 * lines go straight to a speech synthesiser, which stumbles over commas and
 * reads "1." aloud. */
const char *kGrammar = R"(
root ::= [a-zA-Z' \n]+
)";

/* The prompt that actually works on a 1.5B model.
 *
 * The obvious phrasing -- "write four answers the patient might give" -- fails
 * badly and instructively: the model casts ITSELF as the patient and writes one
 * long conversational reply ("I'm not in pain, but my muscles feel stiff..."),
 * which yielded 2 usable answers out of 24. It has to be told it is writing
 * OPTIONS FOR someone else, and then shown the shape twice. The worked examples
 * are doing most of the work here; the instructions alone are not enough at
 * this model size.
 *
 * The spread -- agree, decline, ask for something, suggest something else --
 * is taught by the two examples rather than named in the instructions. Spelling
 * it out as a list got "A specific need" offered to the patient as something to
 * say out loud: at this size the model does not reliably distinguish an
 * instruction about answers from an answer.
 *
 * Kept short regardless. Every extra sentence is latency on every question. */
std::string buildPrompt(const QString &question, const QString &patient_name,
                        const QString &about, const QStringList &history)
{
    std::string p =
        "A caregiver is speaking to a paralysed patient who cannot speak or "
        "move. You write the four answers the patient can choose from. They "
        "pick one by blinking, and it is read aloud in their voice.\n\n"
        "Write EXACTLY four answers, one per line, and nothing else. No "
        "numbering, no introduction, no explanation.\n"
        "Each answer is 3 to 7 words, written as the patient, and answers the "
        "question directly.\n"
        "The four must not overlap in meaning.\n\n"
        "Caregiver: \"Are you in pain?\"\n"
        "I am in pain\n"
        "I feel fine right now\n"
        "My back is hurting\n"
        "Please call the nurse\n\n"
        "Caregiver: \"Do you want water?\"\n"
        "Yes I want water\n"
        "No thank you\n"
        "I want juice instead\n"
        "I am not thirsty\n\n";

    /* The profile already carries the name, so the two are alternatives rather
     * than both -- saying it twice is prompt length for nothing. */
    if (!about.trimmed().isEmpty())
        p += about.trimmed().toStdString() + "\n\n";
    else if (!patient_name.trimmed().isEmpty())
        p += "The patient's name is " + patient_name.trimmed().toStdString() + ".\n\n";

    if (!history.isEmpty()) {
        p += "Earlier in this conversation:\n";
        for (const QString &line : history) p += line.toStdString() + "\n";
        p += "\n";
    }

    p += "Caregiver: \"" + question.trimmed().toStdString() + "\"\n";
    return p;
}

} // namespace

struct Generator::Impl {
    QString model_path;
    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    const llama_vocab *vocab = nullptr;
    bool ready = false;
};

Generator::Generator(QString model_path, QObject *parent)
    : QObject(parent), d(new Impl)
{
    d->model_path = std::move(model_path);
}

Generator::~Generator()
{
    if (d->ctx) llama_free(d->ctx);
    if (d->model) llama_model_free(d->model);
    delete d;
}

void Generator::load()
{
    if (!QFileInfo::exists(d->model_path)) {
        emit failed(QStringLiteral("No language model installed — using the built-in answers."));
        return;
    }

    llama_backend_init();
    llama_log_set([](ggml_log_level, const char *, void *) {}, nullptr);

    llama_model_params mp = llama_model_default_params();
    /* CPU only for now. The GPU path needs a Vulkan-enabled ggml build, and at
     * ~30 generated tokens the CPU is already fast enough not to be the thing
     * anyone notices. */
    mp.n_gpu_layers = 0;

    d->model = llama_model_load_from_file(d->model_path.toUtf8().constData(), mp);
    if (!d->model) {
        emit failed(QStringLiteral("The language model could not be loaded — using the built-in answers."));
        return;
    }

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = 2048;
    cp.n_batch = 512;
    cp.no_perf = true;
    /* Default is a conservative handful of threads. Leave a couple of cores for
     * the camera pipeline, which must keep hitting 30fps while this runs. */
    const int cores = QThread::idealThreadCount();
    cp.n_threads = qBound(2, cores - 2, 8);
    cp.n_threads_batch = cp.n_threads;

    d->ctx = llama_init_from_model(d->model, cp);
    if (!d->ctx) {
        llama_model_free(d->model);
        d->model = nullptr;
        emit failed(QStringLiteral("The language model could not be started — using the built-in answers."));
        return;
    }

    d->vocab = llama_model_get_vocab(d->model);
    d->ready = true;
    emit loaded(QFileInfo(d->model_path).fileName());
}

void Generator::generate(quint64 request_id, const QString &question,
                         const QString &patient_name, const QString &about,
                         const QStringList &history)
{
    if (!d->ready) return;

    QElapsedTimer clock;
    clock.start();

    const std::string user = buildPrompt(question, patient_name, about, history);

    /* Use the model's own chat template. Qwen expects ChatML; feeding it a raw
     * prompt works but measurably worsens instruction following. */
    std::string prompt;
    const char *tmpl = llama_model_chat_template(d->model, nullptr);
    if (tmpl) {
        llama_chat_message msg{ "user", user.c_str() };
        std::vector<char> buf(user.size() + 2048);
        int n = llama_chat_apply_template(tmpl, &msg, 1, true, buf.data(),
                                          static_cast<int32_t>(buf.size()));
        if (n > 0 && n <= static_cast<int>(buf.size())) prompt.assign(buf.data(), n);
    }
    if (prompt.empty()) prompt = user;

    std::vector<llama_token> tokens(prompt.size() + 64);
    int n_tok = llama_tokenize(d->vocab, prompt.c_str(),
                               static_cast<int32_t>(prompt.size()), tokens.data(),
                               static_cast<int32_t>(tokens.size()), true, true);
    if (n_tok <= 0) return;
    tokens.resize(n_tok);

    /* A fresh KV cache per question. The prompt changes every time anyway, and
     * carrying state between questions risks one answer bleeding into the next. */
    llama_memory_clear(llama_get_memory(d->ctx), true);

    llama_batch batch = llama_batch_get_one(tokens.data(), n_tok);
    if (llama_decode(d->ctx, batch) != 0) return;

    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());

    /* Grammar first, deliberately, despite the cost.
     *
     * It must see the full distribution: if top-k ran first and none of its
     * survivors were grammar-valid, every candidate would be masked to -inf and
     * the sampler would return a token the grammar then refuses to accept --
     * which throws. Correctness before speed here; the measured cost is in
     * docs/PLAN.md. */
    if (!qEnvironmentVariableIsSet("OC_NO_GRAMMAR")) {
        if (llama_sampler *grammar = llama_sampler_init_grammar(d->vocab, kGrammar, "root"))
            llama_sampler_chain_add(chain, grammar);
    }
    /* Enough variation that the four answers differ from each other, little
     * enough that they stay on topic. */
    if (qEnvironmentVariableIsSet("OC_GREEDY")) {
        llama_sampler_chain_add(chain, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(chain, llama_sampler_init_top_k(40));
        llama_sampler_chain_add(chain, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    std::string text;
    int newlines = 0;
    /* Four lines of at most seven short words cannot need more than this. The
     * cap exists so a pathological sample cannot hang the thread. */
    constexpr int kMaxTokens = 120;

    for (int i = 0; i < kMaxTokens && newlines < 4; i++) {
        llama_token tok = llama_sampler_sample(chain, d->ctx, -1);
        if (llama_vocab_is_eog(d->vocab, tok)) break;
        llama_sampler_accept(chain, tok);

        char piece[256];
        int n = llama_token_to_piece(d->vocab, tok, piece, sizeof piece, 0, false);
        if (n > 0) {
            text.append(piece, n);
            for (int k = 0; k < n; k++) if (piece[k] == '\n') newlines++;
        }

        llama_batch next = llama_batch_get_one(&tok, 1);
        if (llama_decode(d->ctx, next) != 0) break;
    }

    llama_sampler_free(chain);

    QStringList lines;
    for (const QString &l : QString::fromStdString(text).split(QLatin1Char('\n')))
        if (!l.trimmed().isEmpty()) lines << l.trimmed();

    emit produced(request_id, lines, static_cast<double>(clock.elapsed()));
}
