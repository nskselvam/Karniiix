# KARNIX Local LLM

The hosted C++20 client in `llm/` connects to the Ollama API on loopback only.
In the x86_64 QEMU window, the `ASK <prompt>` shell command sends text over
the emulated COM1 serial port. A separate host-side companion receives it
through a Unix socket, calls this local Ollama client, and returns the answer
over serial. MedGemma inference therefore runs in Ollama on the Mac, not
inside the bare-metal guest. Directly booting the ISO does not start a host
bridge. The 32-bit kernel remains separate and has no LLM connection.

## Model and prerequisites

Default model: `medgemma1.5:4b`. The verified development machine has Ollama
0.40.2, `medgemma1.5:4b`, and `qwen3:4b` installed. Qwen is not used as an
automatic fallback. Choose a model with `KARNIX_MODEL` or `--model`.

MedGemma is a medical text/image research model. Review and comply with
[Google's HAI-DEF terms](https://developers.google.com/health-ai-developer-foundations/terms)
and prohibited-use policy. The model is not clinically validated; its output
is not a diagnosis. This software does not provide patient-specific medical
decision-making or clinical validation.

Start local Ollama and install the model if necessary:

```bash
brew services start ollama
ollama pull medgemma1.5:4b
```

The application itself does not download models. Verify readiness:

```bash
./llm/build/karnix_llm --status
```

## Build and use

```bash
cmake -S llm -B llm/build -DCMAKE_BUILD_TYPE=Release
cmake --build llm/build --parallel
ctest --test-dir llm/build --output-on-failure

./llm/build/karnix_llm --interactive
./llm/build/karnix_llm "Explain a matrix"
./llm/build/karnix_llm --image "/path/to/image.png" "Describe this image"
```

For the interactive x86_64 KarnixOS shell, first ensure Ollama is running and
the chosen model is installed. Then launch:

If Ollama is not already running, start it in a separate terminal and leave
it running:

```bash
ollama serve
```

Then install the model once and launch the OS:

```bash
ollama pull medgemma1.5:4b
make -C os/karnix64 run
```

In the QEMU window, click to focus it, enter `help`, then ask a text question
with `ask <prompt>`. The launcher builds
`karnix_qemu_medgemma_bridge`; it uses a per-run Unix-domain socket with
current-user-only permissions, not a network listener. `KARNIX_MODEL` can
select another model already installed in Ollama, such as `medgemma:4b`.
`KARNIX_OLLAMA_URL` is not supported by the bridge; its API endpoint remains
restricted to `http://127.0.0.1:11434`.
The guest `ASK` command is currently text-only; image attachments remain
available through the hosted CLI/viewer, not through the bare-metal shell.

The hosted ncurses viewer can also invoke the client independently. From that
viewer, enter `llm` for interactive chat or use a one-shot command:

```text
llm "Explain a matrix"
llm --image "/path/to/image.png" "Describe this image"
```

Interactive commands: `/help`, `/clear`, `/image <path> [prompt]`,
`/model [name]`, `/status`, `/exit`. Image paths with spaces must be quoted.
The CLI also accepts multiple `--image <path>` arguments.

## Image and privacy behavior

- Supports PNG and JPEG signatures, up to four images per prompt, 10 MiB per
  image.
- Files are read and base64-encoded locally, then included in the Ollama chat
  request. The endpoint is restricted to `localhost`/`127.0.0.1` HTTP and
  proxy bypass is enabled for loopback.
- The four most recent image attachments are retained for follow-up turns;
  text history is bounded to 16,000 characters.
- No cloud endpoint, API key, shell/tool execution, automatic model download,
  or model-controlled action is used.
- Image generation was tested with a non-clinical chart image. That test
  verifies wiring only, not the accuracy of image interpretation.

## Implementation details

The API uses Ollama's `/api/chat` endpoint with JSON messages. The client
streams newline-delimited JSON, suppresses MedGemma internal-thought and image
boundary markers, imposes request timeouts, handles missing service/model
errors, and supports Ctrl+C cancellation. Request construction and history
management are tested against a local mock server. Real model inference is
reported separately in [`KARNIX_TESTING.md`](KARNIX_TESTING.md).
