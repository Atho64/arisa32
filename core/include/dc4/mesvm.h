// D.C.4 MES control-flow interpreter. Graphics/audio commands are yielded to
// the host; flow, variables and choice definitions are executed here.
// Evidence and deliberate compatibility limits: docs/vm-progress.md.
#pragma once
#include "dc4/messtream.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace dc4 {

struct MesChoice {
    std::string label;
    std::string text;  // decoded CP932 bytes, like MesCommand::str
};

// Engine-owned save data, deliberately independent of original adv32 saves.
// Serialize fields explicitly; this C++ object's memory layout is not a format.
struct MesVmSnapshot {
    static constexpr uint32_t kVersion = 1;
    uint32_t version = kVersion;
    uint64_t script_fingerprint = 0;
    uint32_t pc = 3;
    std::map<std::pair<uint16_t, uint16_t>, int32_t> variables;
    std::string entry_label;
    std::vector<MesChoice> choices;
    bool ended = false;
    bool transferring = false;
    bool waiting_for_choice = false;
};

class MesVm {
public:
    MesVm() = default;
    explicit MesVm(const MesFile& mes) { bind(mes); }

    // The MesFile must outlive its binding. Scene transfers preserve variables
    // and apply an optional 0x51 entry label. reset() starts a fresh game.
    bool bind(const MesFile& mes);
    void reset();

    // Returns the next command for the graphics/audio/text host. False means
    // a choice, script transfer, end, or error; inspect the accessors below.
    // 0x4F/0x50 are yielded once, then next() waits for the host to bind a file.
    bool next(MesCommand* out);
    uint32_t pos() const;
    bool ended() const { return ended_; }
    bool transferring() const { return transferring_; }
    const std::string& error() const { return error_; }

    bool waiting_for_choice() const { return waiting_for_choice_; }
    const std::vector<MesChoice>& choices() const { return choices_; }
    bool choose(size_t index);
    bool jump(const std::string& label);

    // Byte banks truncate on writes exactly as the original does. System bank
    // 99 holds host-supplied state; absent engine services read as zero.
    int32_t read_variable(uint16_t bank, uint16_t index) const;
    void write_variable(uint16_t bank, uint16_t index, int32_t value);

    // Capture requires a bound, non-faulted VM. Restore requires the matching
    // script already bound; reset()+bind() first when loading an unrelated
    // session, so a pending 0x51 entry label cannot affect that bind.
    // Failed validation leaves the live VM unchanged and reports only via err.
    bool capture_snapshot(MesVmSnapshot* out, std::string* err = nullptr) const;
    bool restore_snapshot(const MesVmSnapshot& snapshot, std::string* err = nullptr);

private:
    bool fail(const std::string& message, uint32_t at);
    bool read_command(MesCommand* command);
    bool consume_companion(MesCommand* command, const char* purpose);
    bool skip_scope(uint8_t scope);
    bool execute_compare(const MesCommand& command);
    bool execute_sum(const MesCommand& command);

    const MesFile* mes_ = nullptr;
    std::unique_ptr<MesStream> stream_;
    std::map<std::string, uint32_t> labels_;
    std::map<std::pair<uint16_t, uint16_t>, int32_t> variables_;
    std::vector<MesChoice> choices_;
    std::string entry_label_;
    std::string error_;
    bool waiting_for_choice_ = false;
    bool transferring_ = false;
    bool ended_ = false;
};

}  // namespace dc4
