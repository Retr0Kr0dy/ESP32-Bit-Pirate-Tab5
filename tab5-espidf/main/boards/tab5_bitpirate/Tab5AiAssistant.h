#pragma once

/*
 * Tab5AiAssistant — the "ai" command, a native port of the web UI's "✨ AI"
 * panel (webui/scripts.h's callGeminiTranslate()): same model, same system
 * prompt, same request/response shape, but a command ("ai <question>",
 * "ai key <API_KEY>") instead of a dedicated prompt box -- reuses the
 * terminal already on screen. API key lives in its own NVS namespace,
 * unrelated to BootModeConfigurator's one-shot-boot flag.
 */

#include <string>

class ITerminalView;

namespace tab5ai {

// subcommand/args are TerminalCommand::getSubcommand()/getArgs().
void handleCommand(const std::string& subcommand, const std::string& args, ITerminalView& terminalView);

} // namespace tab5ai
