#include "dc4/mesvm.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool value, const char* expression, int line) {
    if (!value) {
        std::cerr << "line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

struct Script {
    dc4::MesFile file;
    Script() { file.code = {0xB6, 0xAA, 0}; }
    void byte(uint8_t value) { file.code.push_back(value); }
    void word(uint16_t value) { byte(uint8_t(value)); byte(uint8_t(value >> 8)); }
    void pair(uint8_t op, uint8_t a, uint8_t b) { byte(op); byte(a); byte(b); }
    void words(uint8_t op, uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
        byte(op); word(a); word(b); word(c); word(d);
    }
    void string(uint8_t op, const std::string& value) {
        byte(op);
        for (unsigned char ch : value) byte(op >= 0x69 ? uint8_t(ch - 0x20) : ch);
        byte(0);
    }
    void message(const std::string& value) { string(0x6B, value); }
    void end() { byte(0); }
};

std::vector<std::string> messages(dc4::MesVm& vm) {
    std::vector<std::string> output;
    dc4::MesCommand command;
    size_t count = 0;
    while (count++ < 100000 && vm.next(&command)) {
        if (command.op == 0x6B) output.push_back(command.str);
    }
    CHECK(count < 100000);
    return output;
}

void variable_arithmetic() {
    Script script;
    script.words(0x82, 3, 5, 255, 0);
    script.words(0x82, 3, 5, 2, 1);  // 8-bit wrap: 255+2 = 1
    script.words(0x82, 3, 5, 3, 2);  // 1-3 = 254
    script.words(0x83, 9, 7, 3, 5);
    script.words(0x82, 3, 6, 2, 0);
    script.words(0x85, 3, 5, 3, 6);  // 254+2 = 0
    script.words(0x84, 3, 8, 0, 0);
    script.words(0x84, 9, 7, 0, 0);
    script.words(0x84, 3, 6, 0, 0);
    script.pair(0x02, 0, 0);
    script.words(0x82, 93, 0, 0xFFFF, 0); // signed immediate -1, dword bank
    script.end();
    dc4::MesVm vm(script.file);
    CHECK(messages(vm).empty());
    CHECK(vm.ended());
    CHECK(vm.error().empty());
    CHECK(vm.read_variable(3, 5) == 0);
    CHECK(vm.read_variable(9, 7) == 254);
    CHECK(vm.read_variable(3, 8) == 0);
    CHECK(vm.read_variable(93, 0) == -1);
    vm.write_variable(3, 100, 7);
    CHECK(vm.read_variable(3, 100) == 0);
    vm.write_variable(100, 149, -1);
    CHECK(vm.read_variable(100, 149) == 255);
    vm.write_variable(99, 254, 255);
    CHECK(vm.read_variable(99, 254) == -1);
    vm.write_variable(99, 218, 1);
    vm.write_variable(99, 217, 42);
    CHECK(vm.read_variable(93, 1) == 42);
    vm.write_variable(93, 1, 51);
    CHECK(vm.read_variable(99, 217) == 51);
}

void conditional_scopes() {
    // True and false cases for each recovered comparison operator.
    const int left[] = {5, 5, 6, 4, 5, 5};
    const int right[] = {5, 4, 5, 5, 5, 5};
    for (int op = 0; op < 6; ++op) {
        for (int indirect = 0; indirect < 2; ++indirect) {
            Script script;
            script.words(0x86, 3, 0, uint16_t(indirect ? 0x0301 : right[op]), uint16_t(op | (indirect ? 0x80 : 0)));
            script.pair(0x02, 1, 0);
            script.message("then");
            script.pair(0x20, 0, 1);
            script.message("else");
            script.pair(0x20, 1, 1);
            script.message("after");
            script.end();
            dc4::MesVm vm(script.file);
            vm.write_variable(3, 0, left[op]);
            vm.write_variable(3, 1, right[op]);
            CHECK(messages(vm) == (std::vector<std::string>{"then", "after"}));
            CHECK(vm.error().empty());
            CHECK(vm.bind(script.file));
            const int false_left[] = {4, 4, 5, 5, 4, 6};
            vm.write_variable(3, 0, false_left[op]);
            CHECK(messages(vm) == (std::vector<std::string>{"else", "after"}));
        }
    }

    Script nested;
    nested.words(0x86, 3, 0, 1, 0);
    nested.pair(0x02, 1, 0);
    nested.words(0x86, 3, 0, 0, 0);
    nested.pair(0x02, 2, 0);
    nested.message("unreachable");
    nested.pair(0x20, 1, 2);  // inner scope must not finish the outer skip
    nested.message("also_unreachable");
    nested.pair(0x20, 1, 1);
    nested.words(0x86, 99, 254, 0xFFFF, 0);
    nested.pair(0x02, 3, 0);
    nested.message("signed");
    nested.pair(0x20, 1, 3);
    nested.end();
    dc4::MesVm vm(nested.file);
    vm.write_variable(99, 254, 255);
    CHECK(messages(vm) == (std::vector<std::string>{"signed"}));
}

void checkpoints_and_labels() {
    Script script;
    script.string(0x4B, "autoloadmes");
    script.pair(0x1C, 0, 0);
    script.string(0x54, "autoloadmes"); // stored checkpoint companion, not an executed jump
    script.string(0x54, "go");
    script.message("unreachable");
    script.string(0x4B, "go");
    script.message("first");
    script.string(0x54, "end");
    script.string(0x4B, "go");
    script.message("duplicate");
    script.string(0x4B, "end");
    script.end();
    dc4::MesVm vm(script.file);
    CHECK(messages(vm) == (std::vector<std::string>{"first"}));
    CHECK(vm.error().empty());
    CHECK(!vm.jump("GO"));  // original strcmp is case-sensitive
}

void choices_and_transfers() {
    Script script;
    script.pair(0x09, 1, 0);
    script.string(0x5C, "left");
    script.string(0x6C, "Left");
    script.string(0x5C, "right");
    script.string(0x6C, "Right");
    script.pair(0x0A, 0, 0);
    script.string(0x4B, "left");
    script.message("left_branch");
    script.string(0x54, "join");
    script.string(0x4B, "right");
    script.message("right_branch");
    script.string(0x4B, "join");
    script.message("common");
    script.words(0x82, 9, 7, 42, 0);
    script.string(0x51, "entry");
    script.string(0x50, "next_script");
    script.message("past_transfer");
    script.end();
    Script target;
    target.message("before_entry");
    target.string(0x4B, "entry");
    target.message("after_entry");
    target.end();

    for (size_t choice = 0; choice < 2; ++choice) {
        dc4::MesVm vm(script.file);
        CHECK(messages(vm).empty());
        CHECK(vm.waiting_for_choice());
        CHECK(!vm.ended());
        CHECK(vm.choices().size() == 2);
        CHECK(vm.choices()[0].text == "Left");
        CHECK(vm.choices()[1].label == "right");
        CHECK(!vm.choose(2));
        CHECK(vm.waiting_for_choice());
        CHECK(vm.choose(choice));
        CHECK(messages(vm) == (std::vector<std::string>{choice ? "right_branch" : "left_branch", "common"}));
        CHECK(vm.transferring());
        CHECK(vm.bind(target.file));
        CHECK(vm.read_variable(9, 7) == 42);
        CHECK(messages(vm) == (std::vector<std::string>{"after_entry"}));
        vm.reset();
        CHECK(vm.read_variable(9, 7) == 0);
    }
}

void malformed_scripts() {
    Script missing_scope;
    missing_scope.words(0x86, 3, 0, 1, 0);
    missing_scope.pair(0x02, 1, 0);
    missing_scope.message("unreachable");
    missing_scope.end();
    dc4::MesVm vm(missing_scope.file);
    CHECK(messages(vm).empty());
    CHECK(!vm.error().empty());

    Script truncated;
    truncated.byte(0x82);
    CHECK(!vm.bind(truncated.file));
    CHECK(vm.error().find("truncated") != std::string::npos);

    Script loop;
    loop.string(0x4B, "loop");
    loop.string(0x54, "loop");
    loop.end();
    CHECK(vm.bind(loop.file));
    CHECK(messages(vm).empty());
    CHECK(vm.error().find("budget") != std::string::npos);

    Script missing_choice;
    missing_choice.string(0x5C, "label");
    missing_choice.end();
    CHECK(vm.bind(missing_choice.file));
    CHECK(messages(vm).empty());
    CHECK(!vm.error().empty());
}

void snapshots() {
    Script script;
    script.words(0x82, 3, 5, 42, 0);
    script.message("first");
    script.pair(0x09, 1, 0);
    script.string(0x5C, "left");
    script.string(0x6C, "Left");
    script.string(0x5C, "right");
    script.string(0x6C, "Right");
    script.pair(0x0A, 0, 0);
    script.string(0x4B, "left");
    script.message("left");
    script.string(0x54, "join");
    script.string(0x4B, "right");
    script.message("right");
    script.string(0x4B, "join");
    script.words(0x82, 3, 5, 1, 1);
    script.string(0x51, "entry");
    script.string(0x4F, "next");
    script.end();
    Script destination;
    destination.message("unreachable");
    destination.string(0x4B, "entry");
    destination.message("destination");
    destination.end();

    dc4::MesVm vm(script.file);
    dc4::MesCommand command;
    dc4::MesVmSnapshot running, waiting, transferring, finished;
    std::string error;
    CHECK(vm.next(&command) && command.str == "first");
    CHECK(vm.capture_snapshot(&running, &error));
    CHECK(error.empty());
    CHECK(running.version == dc4::MesVmSnapshot::kVersion);
    CHECK(running.variables.at({uint16_t(3), uint16_t(5)}) == 42);
    CHECK(messages(vm).empty());
    CHECK(vm.waiting_for_choice());
    CHECK(vm.capture_snapshot(&waiting));
    CHECK(waiting.waiting_for_choice && waiting.choices.size() == 2);

    // Corrupt/incompatible input must not replace any part of live state.
    const auto reject_unchanged = [&](const dc4::MesVmSnapshot& invalid) {
        CHECK(!vm.restore_snapshot(invalid, &error));
        CHECK(!error.empty());
        CHECK(vm.error().empty());
        CHECK(vm.pos() == waiting.pc);
        CHECK(vm.waiting_for_choice());
        CHECK(vm.choices().size() == 2);
        CHECK(vm.read_variable(3, 5) == 42);
    };
    auto invalid = waiting;
    invalid.version = dc4::MesVmSnapshot::kVersion + 1;
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.script_fingerprint ^= 1;
    reject_unchanged(invalid);
    invalid = waiting;
    ++invalid.pc;  // inside the following label string
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.pc = uint32_t(script.file.code.size() + 1);
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.ended = true;
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.choices.clear();
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.choices[0].label = "missing";
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.variables[{uint16_t(3), uint16_t(5)}] = 256;
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.variables[{uint16_t(93), uint16_t(20)}] = 1;
    reject_unchanged(invalid);
    invalid = waiting;
    invalid.pc = running.pc; // a valid boundary, but not after choice activation
    reject_unchanged(invalid);

    // Content identity, not the MesFile address/path, binds a snapshot.
    dc4::MesFile copy = script.file;
    dc4::MesVm other(copy);
    CHECK(other.restore_snapshot(waiting));
    copy.code[4] ^= 1;
    CHECK(other.bind(copy));
    CHECK(!other.restore_snapshot(waiting));
    copy = script.file;
    copy.version ^= 1;
    CHECK(other.bind(copy));
    CHECK(!other.restore_snapshot(waiting));
    copy = script.file;
    copy.labels.push_back({3, 0, 1});
    CHECK(other.bind(copy));
    CHECK(!other.restore_snapshot(waiting));

    CHECK(vm.choose(0));
    CHECK(messages(vm) == (std::vector<std::string>{"left"}));
    CHECK(vm.capture_snapshot(&transferring));
    CHECK(transferring.transferring);
    CHECK(transferring.entry_label == "entry");
    vm.write_variable(3, 7, 99);
    CHECK(vm.restore_snapshot(waiting));
    CHECK(vm.read_variable(3, 5) == 42);
    CHECK(vm.read_variable(3, 7) == 0);
    CHECK(vm.choose(1));
    CHECK(messages(vm) == (std::vector<std::string>{"right"}));
    CHECK(vm.restore_snapshot(running));
    CHECK(!vm.waiting_for_choice() && !vm.transferring());
    CHECK(messages(vm).empty()); // the already-shown first line is not replayed
    CHECK(vm.waiting_for_choice());

    // A valid save can recover a faulted VM; capturing the fault is refused.
    CHECK(!vm.jump("missing"));
    invalid.version = 999;
    CHECK(!vm.capture_snapshot(&invalid));
    CHECK(invalid.version == 999);
    CHECK(vm.restore_snapshot(transferring));
    CHECK(vm.error().empty());
    CHECK(vm.transferring());
    CHECK(vm.bind(destination.file));
    CHECK(vm.read_variable(3, 5) == 43);
    CHECK(messages(vm) == (std::vector<std::string>{"destination"}));
    CHECK(vm.capture_snapshot(&finished));
    CHECK(finished.ended);
    CHECK(vm.bind(destination.file));
    CHECK(vm.restore_snapshot(finished));
    CHECK(vm.ended());
    CHECK(!vm.next(&command));
    vm.reset();
    CHECK(!vm.capture_snapshot(&invalid));
    CHECK(!vm.restore_snapshot(finished));
    CHECK(vm.bind(destination.file));
    CHECK(vm.restore_snapshot(finished));
}

void real_script_regressions(const std::string& mes_dir) {
    dc4::MesFile file;
    std::string error;
    if (!file.load_file(mes_dir + "/dc4_kyo20190204a1.mes", &error)) {
        std::cerr << error << '\n';
        CHECK(false);
        return;
    }
    dc4::MesVm vm(file);
    dc4::MesCommand command;
    size_t dialogue_count = 0;
    std::string transfer;
    while (vm.next(&command)) {
        if (command.op == 0x6B) ++dialogue_count;
        if (command.op == 0x4F || command.op == 0x50) transfer = command.str;
    }
    CHECK(vm.error().empty());
    CHECK(dialogue_count > 10);
    CHECK(transfer == "DC4_KYO20190204A2");

    if (!file.load_file(mes_dir + "/dc4_dc99990101a.mes", &error)) {
        std::cerr << error << '\n';
        CHECK(false);
        return;
    }
    for (size_t choice = 0; choice < 2; ++choice) {
        vm.reset();
        CHECK(vm.bind(file));
        while (vm.next(&command)) {}
        CHECK(vm.error().empty());
        CHECK(vm.waiting_for_choice());
        CHECK(vm.choices().size() == 2);
        CHECK(vm.choose(choice));
        bool first_line = true;
        bool rejoined = false;
        while (vm.next(&command)) {
            if (command.op != 0x6B) continue;
            if (first_line) {
                CHECK(command.pos == (choice ? 4944u : 4718u));
                first_line = false;
            }
            CHECK(command.pos != (choice ? 4718u : 4944u));
            if (command.pos == 5094) { rejoined = true; break; }
        }
        CHECK(rejoined);
        CHECK(vm.error().empty());
    }

    // This chapter intentionally repeats the menu after a wrong password.
    // Choosing the fourth option exits; repeat menus must clear old choices.
    if (!file.load_file(mes_dir + "/dc4_sor20190504a.mes", &error)) {
        std::cerr << error << '\n';
        CHECK(false);
        return;
    }
    vm.reset();
    CHECK(vm.bind(file));
    while (vm.next(&command)) {}
    CHECK(vm.waiting_for_choice());
    CHECK(vm.choices().size() == 5);
    CHECK(vm.choose(0));
    while (vm.next(&command)) {}
    CHECK(vm.waiting_for_choice());
    CHECK(vm.choices().size() == 5);
    CHECK(vm.choose(3));
    bool opened = false;
    while (vm.next(&command)) {
        if (command.op == 0x6B) { opened = command.pos == 27447; break; }
    }
    CHECK(opened);
    CHECK(vm.error().empty());
}

void chapter_corpus_smoke(const std::string& mes_dir) {
    size_t chapters = 0, choice_count = 0, dialogue_count = 0, transfers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(mes_dir)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("dc4_", 0) != 0 || entry.path().extension() != ".mes") continue;
        ++chapters;
        dc4::MesFile file;
        std::string error;
        if (!file.load_file(entry.path().string(), &error)) {
            std::cerr << error << '\n';
            CHECK(false);
            continue;
        }
        dc4::MesVm vm(file);
        dc4::MesCommand command;
        size_t steps = 0, local_choices = 0;
        for (; steps < 100000; ++steps) {
            if (vm.next(&command)) {
                if (command.op == 0x6B) ++dialogue_count;
                if (command.op == 0x4F || command.op == 0x50) ++transfers;
            } else if (vm.waiting_for_choice()) {
                ++choice_count;
                // Rotate on repeated menus: always choosing the first option
                // intentionally loops in the password puzzle above.
                CHECK(vm.choose(local_choices++ % vm.choices().size()));
            } else {
                break;
            }
        }
        if (!vm.error().empty() || steps == 100000)
            std::cerr << name << ": " << vm.error() << " steps=" << steps << '\n';
        CHECK(vm.error().empty());
        CHECK(steps < 100000);
    }
    CHECK(chapters > 0);
    std::cout << "Chapter smoke: " << chapters << " chapters, " << dialogue_count
              << " dialogue commands, " << choice_count << " choices, "
              << transfers << " transfers\n";
}
}  // namespace

int main(int argc, char** argv) {
    variable_arithmetic();
    conditional_scopes();
    checkpoints_and_labels();
    choices_and_transfers();
    malformed_scripts();
    snapshots();
    if (argc == 2) {
        real_script_regressions(argv[1]);
        chapter_corpus_smoke(argv[1]);
    }
    if (failures) {
        std::cerr << failures << " MES VM regression check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "MES VM regressions passed" << (argc == 2 ? " (including D.C.4 scripts)" : "") << '\n';
    return EXIT_SUCCESS;
}
