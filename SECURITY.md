<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Reporting a security problem

Please report privately rather than opening a public issue. Use GitHub's
**Report a vulnerability** button under the Security tab, which is private to
the maintainers.

Expect an acknowledgement within a week. If you have not heard back in two,
assume the message went astray and open a public issue saying only that you are
waiting for a security contact — no details.

## What is in scope

OpennComm runs entirely on one machine, with no network listener and no
accounts, so the usual web attack surface does not exist here. What does matter:

- **Anything that causes a wrong answer to be spoken.** Speaking a sentence the
  patient did not choose is the worst outcome in this system, worse than
  speaking nothing. A crash is a bug; a plausible-but-unchosen utterance is a
  security problem.
- **Anything that leaks conversation history off the machine.** `QAHistory`-style
  data is medical information about someone who often cannot consent to its
  disclosure. Nothing here should ever make a network request.
- **Anything that lets a downloaded model or voice execute code.** Models are
  fetched over HTTPS from Hugging Face and GitHub; a path traversal or archive
  extraction flaw in that fetching is in scope.
- Memory-safety faults in `core/`, which parses no untrusted input but does run
  on every camera frame.

## What is not

- The absence of authentication. There is deliberately no login: this is a
  single-user application on a machine in someone's home or ward.
- The models' own behaviour. A language model writing an odd answer is a quality
  problem, not a vulnerability — the patient chooses among options and nothing
  is spoken without a deliberate selection.
