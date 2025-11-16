#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <queue>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;

// Instruction types
enum InsnType { BRANCH, STORE, LOAD, ARITHMETIC, NOP, SPLOOP, SPKERNEL, SPMASK, OTHER };

// Instruction structure
struct Instruction {
    InsnType type;
    string mnemonic;
    string unit;
    int delay_slots;
    string predicate;
    string operands;
    int line_num;
    bool parallel;
};

// Execute Packet (EP) - instructions executed in parallel
struct ExecutePacket {
    vector<Instruction> instructions;
    int cycles;
    int ep_num;
};

// Translation Block (TB)
struct TranslationBlock {
    vector<ExecutePacket> packets;
    int tb_id;
    int max_cycles;
    string start_label;
    int start_ep_index;
    int end_ep_index;
};

// Context for saving unexpired instructions
struct SavedContext {
    int remaining_delay;
    string target_address;
    int instruction_line;
};

// Simulator state
class VLIWSimulator {
private:
    vector<ExecutePacket> guest_code;
    map<string, int> registers;
    vector<TranslationBlock> translation_blocks;
    vector<SavedContext> saved_contexts;
    int current_tb_id;
    int ILC;    // Inner Loop Counter
    int RILC;   // Reload Inner Loop Counter
    int state;  // State for software-pipelined loops
    int A1;     // Outer loop counter
    int sploop_start_index; // Index where SPLOOP starts
    
    // Store instruction deferred translation
    struct DeferredStore {
        string operands;
        string unit;
        int line_num;
    };
    vector<DeferredStore> deferred_stores;

public:
    VLIWSimulator() : current_tb_id(0), ILC(0), RILC(0), state(0), A1(0), sploop_start_index(-1) {
        registers["B1"] = 5;
        registers["B3"] = 0;
        registers["A10"] = 100;
        registers["A2"] = 0;
        registers["A4"] = 0;
    }

    Instruction createInstruction(InsnType type, const string& mnemonic, const string& unit,
                                   int delay, const string& operands, int line, 
                                   const string& pred = "", bool par = false) {
        Instruction insn;
        insn.type = type;
        insn.mnemonic = mnemonic;
        insn.unit = unit;
        insn.delay_slots = delay;
        insn.operands = operands;
        insn.line_num = line;
        insn.predicate = pred;
        insn.parallel = par;
        return insn;
    }

    void addEP(int ep_num, int cycles, const Instruction& insn) {
        ExecutePacket ep;
        ep.ep_num = ep_num;
        ep.cycles = cycles;
        ep.instructions.push_back(insn);
        guest_code.push_back(ep);
    }

    void addEP(int ep_num, int cycles, const vector<Instruction>& insns) {
        ExecutePacket ep;
        ep.ep_num = ep_num;
        ep.cycles = cycles;
        ep.instructions = insns;
        guest_code.push_back(ep);
    }

    void parseGuestCode() {
        for (int i = 1; i <= 5; i++) {
            addEP(i, 1, createInstruction(BRANCH, "B", ".S2", 5, "LOOP", i));
        }
        
        vector<Instruction> ep6_insns = {
            createInstruction(ARITHMETIC, "SUB", ".D2", 0, "B1, 0x1, B1", 6, "[B1]"),
            createInstruction(BRANCH, "B", ".S1", 5, "LOOP", 7, "[B1]", true)
        };
        addEP(6, 1, ep6_insns);
        
        addEP(7, 1, createInstruction(BRANCH, "B", ".S2", 5, "B3", 8));
        
        for (int i = 8; i <= 11; i++) {
            addEP(i, 1, createInstruction(NOP, "NOP", "", 0, "", 9 + (i - 8)));
        }
        
        addEP(12, 1, createInstruction(ARITHMETIC, "MV", ".L1", 0, "A10, A2", 12));
        addEP(13, 1, createInstruction(ARITHMETIC, "ADD", ".L1", 0, "A4, A2, A4", 13));
    }
    
    void parseSoftwarePipelinedLoop() {
        guest_code.clear();
        sploop_start_index = -1;
        
        addEP(1, 1, createInstruction(ARITHMETIC, "MVK", ".S", 0, "8, A0", 1));
        addEP(2, 1, createInstruction(ARITHMETIC, "MVC", ".S", 0, "A0, ILC", 2));
        addEP(3, 3, createInstruction(NOP, "NOP", "", 0, "3", 3));
        addEP(4, 1, createInstruction(SPLOOP, "SPLOOP", "", 0, "1", 4));
        sploop_start_index = 4; // EP5 is where the pipelined body starts (index 4 in 0-based)
        addEP(5, 1, createInstruction(LOAD, "LDW", ".D", 4, "*A1++, A2", 5));
        addEP(6, 4, createInstruction(NOP, "NOP", "", 0, "4", 6));
        addEP(7, 1, createInstruction(ARITHMETIC, "MV", ".L1X", 0, "A2, B2", 7));
        
        vector<Instruction> parallel_insns = {
            createInstruction(SPKERNEL, "SPKERNEL", "", 0, "6, 0", 8),
            createInstruction(STORE, "STW", ".D", 0, "B2, *B0++", 9, "", true)
        };
        addEP(8, 1, parallel_insns);
    }
    
    void parseNestedSoftwarePipelinedLoop() {
        guest_code.clear();
        sploop_start_index = -1;
        
        addEP(1, 1, createInstruction(ARITHMETIC, "MVK", ".S", 0, "7, A8", 1));
        addEP(2, 1, createInstruction(ARITHMETIC, "MVC", ".S", 0, "A8, ILC", 2));
        addEP(3, 1, createInstruction(ARITHMETIC, "MVC", ".S", 0, "A8, RILC", 3));
        addEP(4, 1, createInstruction(ARITHMETIC, "MVK", ".S", 0, "1, A1", 4));
        addEP(5, 3, createInstruction(NOP, "NOP", "", 0, "3", 5));
        addEP(6, 1, createInstruction(SPLOOP, "SPLOOP", "", 0, "1", 6, "[A1]"));
        sploop_start_index = 6; // EP7 starts the pipelined body (index 6 in 0-based)
        addEP(7, 1, createInstruction(LOAD, "LDW", ".D1", 0, "*A4++, A0", 7));
        addEP(8, 4, createInstruction(NOP, "NOP", "", 0, "4", 8));
        addEP(9, 1, createInstruction(ARITHMETIC, "MV", ".L2X", 0, "A0, B0", 9));
        
        vector<Instruction> spkernel_insns = {
            createInstruction(SPKERNEL, "SPKERNELR", "", 0, "", 10),
            createInstruction(STORE, "STW", ".D2", 0, "B0, *B4++", 11, "", true)
        };
        addEP(10, 1, spkernel_insns);
        
        addEP(11, 1, createInstruction(BRANCH, "BR", ".S2", 5, "TARGET", 12));
        
        vector<Instruction> spmask_insns = {
            createInstruction(SPMASK, "SPMASK", ".D", 0, "", 13),
            createInstruction(BRANCH, "B", "", 0, "BR TARGET", 14, "[A1]", true),
            createInstruction(ARITHMETIC, "SUB", ".S1", 0, "A1, 1, A1", 15, "[A1]", true),
            createInstruction(LOAD, "LDW", ".D1", 0, "*A6, A0", 16, "[A1]", true),
            createInstruction(ARITHMETIC, "ADD", ".L1", 0, "A6, 4, A4", 17, "[A1]", true)
        };
        addEP(12, 1, spmask_insns);
        
        addEP(13, 4, createInstruction(NOP, "NOP", "", 0, "4", 18));
        addEP(14, 1, createInstruction(ARITHMETIC, "OR", ".S2", 0, "B6, 0, B4", 19));
        addEP(15, 1, createInstruction(NOP, "NOP", "", 0, "", 20));
    }

    TranslationBlock translateWithConstraint(int start_ep, int initial_cycles) {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.max_cycles = initial_cycles;
        tb.start_ep_index = start_ep;
        
        cout << "\n=== TB-Length Constraint Strategy ===" << endl;
        cout << "Translating TB" << tb.tb_id << " starting from EP" << (start_ep + 1) 
             << " with max cycles: " << initial_cycles << endl;
        
        int cycles = initial_cycles;
        int ep_index = start_ep;
        
        while (ep_index < guest_code.size() && cycles > 0) {
            ExecutePacket ep = guest_code[ep_index];
            int consumed_cycles = ep.cycles;
            
            cout << "  Processing EP" << ep.ep_num << " (consumes " 
                 << consumed_cycles << " cycle(s))" << endl;
            
            int min_current_branch_delay = 1000;
            for (const auto& insn : ep.instructions) {
                if (insn.type == BRANCH && insn.delay_slots < min_current_branch_delay) {
                    min_current_branch_delay = insn.delay_slots;
                }
            }
            
            tb.packets.push_back(ep);
            
            for (const auto& insn : ep.instructions) {
                if (insn.type == BRANCH) {
                    SavedContext ctx;
                    ctx.remaining_delay = insn.delay_slots;
                    ctx.target_address = insn.operands;
                    ctx.instruction_line = insn.line_num;
                    saved_contexts.push_back(ctx);
                    
                    cout << "    Saved branch context: delay=" << insn.delay_slots 
                         << ", target=" << insn.operands << endl;
                }
            }
            
            cycles -= consumed_cycles;
            ep_index++;
            
            if (min_current_branch_delay < 1000 && min_current_branch_delay < cycles) {
                cout << "  Branch detected with delay=" << min_current_branch_delay 
                     << ", constraining remaining cycles to " << min_current_branch_delay << endl;
                cycles = min_current_branch_delay;
            }
            
            if (cycles <= 0) {
                cout << "  TB translation terminated (cycles exhausted)" << endl;
                break;
            }
        }
        
        tb.end_ep_index = ep_index - 1;
        cout << "TB" << tb.tb_id << " contains " << tb.packets.size() << " EPs" 
             << " (EP" << (tb.start_ep_index + 1) << " to EP" << (tb.end_ep_index + 1) << ")" << endl;
        return tb;
    }

    void translateEPWithDeferredStores(ExecutePacket& ep) {
        cout << "\n=== Deferring Translation Strategy ===" << endl;
        cout << "Translating EP" << ep.ep_num << endl;
        
        deferred_stores.clear();
        vector<Instruction> translated_insns;
        
        for (const auto& insn : ep.instructions) {
            if (insn.type == STORE) {
                DeferredStore ds;
                ds.operands = insn.operands;
                ds.unit = insn.unit;
                ds.line_num = insn.line_num;
                deferred_stores.push_back(ds);
                
                cout << "  Deferred STORE instruction: " << insn.mnemonic 
                     << " " << insn.operands << endl;
            } else {
                translated_insns.push_back(insn);
                cout << "  Translated: " << insn.mnemonic << " " << insn.operands << endl;
            }
        }
        
        for (const auto& ds : deferred_stores) {
            Instruction store_insn;
            store_insn.type = STORE;
            store_insn.mnemonic = "STW";
            store_insn.unit = ds.unit;
            store_insn.operands = ds.operands;
            store_insn.line_num = ds.line_num;
            translated_insns.push_back(store_insn);
            
            cout << "  Translated deferred STORE: STW " << ds.operands << endl;
        }
        
        ep.instructions = translated_insns;
        cout << "EP translation complete with correct LD/ST ordering" << endl;
    }

    void translateSoftwarePipelinedLoop() {
        cout << "\n=== Software-Pipelined Loop Translation ===" << endl;
        cout << "State: " << state << ", ILC: " << ILC << endl;
        
        if (state == 0) {
            // TRANSLATE ONCE for state 0
            cout << "State 0: Translating first iteration TB (all instructions)" << endl;
            TranslationBlock tb = translateNormalLoop();
            translation_blocks.push_back(tb);
            cout << "Generated TB" << tb.tb_id << " for state 0" << endl;
            
            cout << "\n--- Executing State 0 TB ---" << endl;
            cout << "Iteration 1: Executing TB" << tb.tb_id << " (state 0 - includes all instructions)" << endl;
            
            ILC--;
            if (ILC > 0) {
                state = 1;
                cout << "\nTransitioning to state 1 for subsequent iterations" << endl;
                
                // TRANSLATE ONCE for state 1 (this TB will be executed ILC times)
                cout << "\nState 1: Translating loop kernel TB (skip prolog, will be executed " << ILC << " times)" << endl;
                TranslationBlock tb1 = translateKernelLoop();
                translation_blocks.push_back(tb1);
                cout << "Generated TB" << tb1.tb_id << " for state 1 (reusable)" << endl;
                
                // Now EXECUTE the state 1 TB multiple times (without re-translating)
                cout << "\n--- Executing State 1 TB (Loop Kernel) ---" << endl;
                for (int i = 1; i <= ILC; i++) {
                    cout << "Iteration " << (i + 1) << ": Executing TB" << tb1.tb_id 
                         << " (state 1 - kernel only, ILC=" << (ILC - i + 1) << ")" << endl;
                }
                ILC = 0; // All iterations completed
                state = 0;
                cout << "\nLoop completed, reset to state 0" << endl;
            } else {
                state = 0;
                cout << "Loop completed (only 1 iteration)" << endl;
            }
        }
    }

    void translateNestedLoop() {
        cout << "\n=== Nested Software-Pipelined Loop Translation ===" << endl;
        cout << "State: " << state << ", ILC: " << ILC << ", RILC: " << RILC << ", A1: " << A1 << endl;
        
        if (state == 0) {
            // TRANSLATE ONCE: First iteration of outer loop
            cout << "State 0: Translating prolog TB (first iteration of outer loop)" << endl;
            TranslationBlock tb0 = translateNestedProlog();
            translation_blocks.push_back(tb0);
            cout << "Generated TB" << tb0.tb_id << " for state 0 (prolog)" << endl;
            
            cout << "\n--- Executing State 0 TB (Prolog) ---" << endl;
            cout << "Inner iteration 1: Executing TB" << tb0.tb_id << " (state 0 - prolog)" << endl;
            
            ILC--;
            if (ILC > 0) {
                state = 1;
                cout << "\nTransitioning to state 1 (inner loop body)" << endl;
                
                // TRANSLATE ONCE for state 1 (inner loop body - will be executed ILC times)
                cout << "\nState 1: Translating inner loop body TB (will be executed " << ILC << " times)" << endl;
                TranslationBlock tb1 = translateNestedInner();
                translation_blocks.push_back(tb1);
                cout << "Generated TB" << tb1.tb_id << " for state 1 (reusable inner loop body)" << endl;
                
                // EXECUTE the inner loop body TB multiple times
                cout << "\n--- Executing State 1 TB (Inner Loop Body) ---" << endl;
                for (int i = 1; i <= ILC; i++) {
                    cout << "Inner iteration " << (i + 1) << ": Executing TB" << tb1.tb_id 
                         << " (state 1 - inner body, ILC=" << (ILC - i + 1) << ")" << endl;
                }
                
                ILC = 0; // Inner loop completed
                ILC = RILC;  // Reload for next outer iteration
                A1--;
                
                if (A1 > 0) {
                    state = 2;
                    cout << "\nInner loop completed, reloading ILC=" << RILC << ", transitioning to state 2 (overlap)" << endl;
                } else {
                    state = 0;
                    cout << "\nAll loops completed" << endl;
                }
            } else {
    
                ILC = RILC;
                A1--;
                if (A1 > 0) {
                    state = 2;
                    cout << "\nTransitioning to state 2 (next outer iteration)" << endl;
                } else {
                    state = 0;
                    cout << "\nAll loops completed" << endl;
                }
            }
        } else if (state == 2) {
            // Check if we already have a state 2 TB (reuse it)
            int state2_tb_id = -1;
            for (const auto& tb : translation_blocks) {
                if (tb.start_label == "NESTED_STATE_2") {
                    state2_tb_id = tb.tb_id;
                    break;
                }
            }
            
            if (state2_tb_id == -1) {
                // TRANSLATE ONCE: Overlap section (first time in state 2)
                cout << "State 2: Translating overlap TB (outer epilog + next inner prolog with SPMASK)" << endl;
                TranslationBlock tb2 = translateNestedOverlap();
                translation_blocks.push_back(tb2);
                state2_tb_id = tb2.tb_id;
                cout << "Generated TB" << state2_tb_id << " for state 2 (overlap section)" << endl;
            } else {
                cout << "State 2: Re-using existing overlap TB" << state2_tb_id << endl;
            }
            
            cout << "\n--- Executing State 2 TB (Overlap) ---" << endl;
            cout << "Overlap: Executing TB" << state2_tb_id << " (state 2 - synchronizing loops)" << endl;
            
            // After overlap, the inner loop restarts
            cout << "\n--- Executing State 0 TB (Prolog of new inner loop) ---" << endl;
            // Find and reuse state 0 TB
            int state0_tb_id = -1;
            for (const auto& tb : translation_blocks) {
                if (tb.start_label == "NESTED_STATE_0") {
                    state0_tb_id = tb.tb_id;
                    break;
                }
            }
            if (state0_tb_id != -1) {
                cout << "Inner iteration 1: Re-executing TB" << state0_tb_id << " (state 0 - prolog)" << endl;
            }
            
            ILC--;
            if (ILC > 0) {
                // Re-use the state 1 TB from before
                cout << "\n--- Re-using State 1 TB (Inner Loop Body) ---" << endl;
                int state1_tb_id = -1;
                for (const auto& tb : translation_blocks) {
                    if (tb.start_label == "NESTED_STATE_1") {
                        state1_tb_id = tb.tb_id;
                        break;
                    }
                }
                
                if (state1_tb_id != -1) {
                    for (int i = 1; i <= ILC; i++) {
                        cout << "Inner iteration " << (i + 1) << ": Re-executing TB" << state1_tb_id 
                             << " (state 1 - inner body, ILC=" << (ILC - i + 1) << ")" << endl;
                    }
                }
                
                ILC = 0;
                ILC = RILC;
                A1--;
                
                if (A1 > 0) {
                    state = 2;
                    cout << "\nInner loop completed, staying in state 2 for next outer iteration" << endl;
                } else {
                    state = 0;
                    cout << "\nAll loops completed, reset to state 0" << endl;
                }
            } else {
                ILC = RILC;
                A1--;
                if (A1 > 0) {
                    state = 2;
                } else {
                    state = 0;
                    cout << "\nAll loops completed" << endl;
                }
            }
        }
    }

    TranslationBlock translateNormalLoop() {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.start_label = "LOOP_STATE_0";
        
        cout << "  Translating EPs into TB" << tb.tb_id << ":" << endl;
        for (size_t i = 0; i < guest_code.size(); i++) {
            tb.packets.push_back(guest_code[i]);
            cout << "    EP" << guest_code[i].ep_num << ": ";
            for (const auto& insn : guest_code[i].instructions) {
                if (insn.parallel) cout << "|| ";
                cout << insn.mnemonic;
                if (!insn.operands.empty()) cout << " " << insn.operands;
                cout << " ";
            }
            cout << endl;
        }
        
        cout << "  Generated TB" << tb.tb_id << " with " 
             << tb.packets.size() << " EPs" << endl;
        return tb;
    }

    TranslationBlock translateKernelLoop() {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.start_label = "LOOP_STATE_1";
        
        cout << "  Translating kernel EPs into TB" << tb.tb_id << " (skip prolog):" << endl;
        
        // For simple software-pipelined loops, skip setup instructions (before SPLOOP)
        // and include only the pipelined kernel body
        for (size_t i = 0; i < guest_code.size(); i++) {
            // Skip prolog (instructions before SPLOOP body)
            if (sploop_start_index >= 0 && (int)i < sploop_start_index) {
                cout << "    EP" << guest_code[i].ep_num << ": [SKIPPED - prolog]" << endl;
                continue;
            }
            
            tb.packets.push_back(guest_code[i]);
            cout << "    EP" << guest_code[i].ep_num << ": ";
            for (const auto& insn : guest_code[i].instructions) {
                if (insn.parallel) cout << "|| ";
                cout << insn.mnemonic;
                if (!insn.operands.empty()) cout << " " << insn.operands;
                cout << " ";
            }
            cout << endl;
        }
        
        cout << "  Generated TB" << tb.tb_id << " with " 
             << tb.packets.size() << " EPs (kernel only)" << endl;
        return tb;
    }

    TranslationBlock translateNestedProlog() {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.start_label = "NESTED_STATE_0";
        
        cout << "  Translating prolog instructions into TB" << tb.tb_id << ":" << endl;
        for (int i = 0; i < sploop_start_index; i++) {
            if (i < guest_code.size()) {
                tb.packets.push_back(guest_code[i]);
                cout << "    EP" << guest_code[i].ep_num << ": ";
                for (const auto& insn : guest_code[i].instructions) {
                    if (insn.parallel) cout << "|| ";
                    if (!insn.predicate.empty()) cout << insn.predicate << " ";
                    cout << insn.mnemonic;
                    if (!insn.operands.empty()) cout << " " << insn.operands;
                    cout << " ";
                }
                cout << endl;
            }
        }
        
        cout << "  Generated TB" << tb.tb_id << " (prolog with " << tb.packets.size() << " EPs)" << endl;
        return tb;
    }

    TranslationBlock translateNestedInner() {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.start_label = "NESTED_STATE_1";
        
        cout << "  Translating inner loop body into TB" << tb.tb_id << " (kernel only):" << endl;
        
        // Inner loop body: from sploop_start_index until we hit BRANCH or SPMASK
        for (size_t i = sploop_start_index; i < guest_code.size(); i++) {
            // Check if this EP contains SPMASK or BRANCH (end of kernel)
            bool has_spmask = false;
            bool has_branch = false;
            
            for (const auto& insn : guest_code[i].instructions) {
                if (insn.type == SPMASK) {
                    has_spmask = true;
                }
                if (insn.type == BRANCH) {
                    has_branch = true;
                }
            }
            
            // Stop at BRANCH (marks end of inner kernel)
            if (has_branch) {
                cout << "    EP" << guest_code[i].ep_num << ": [SKIPPED - end of kernel (BRANCH)]" << endl;
                break;
            }
            
            // Skip SPMASK
            if (has_spmask) {
                cout << "    EP" << guest_code[i].ep_num << ": [SKIPPED - contains SPMASK]" << endl;
                continue;
            }
            
            tb.packets.push_back(guest_code[i]);
            cout << "    EP" << guest_code[i].ep_num << ": ";
            for (const auto& insn : guest_code[i].instructions) {
                if (insn.parallel) cout << "|| ";
                if (!insn.predicate.empty()) cout << insn.predicate << " ";
                cout << insn.mnemonic;
                if (!insn.operands.empty()) cout << " " << insn.operands;
                cout << " ";
            }
            cout << endl;
        }
        
        cout << "  Generated TB" << tb.tb_id << " (inner loop body with " << tb.packets.size() << " EPs)" << endl;
        return tb;
    }

    TranslationBlock translateNestedOverlap() {
        TranslationBlock tb;
        tb.tb_id = current_tb_id++;
        tb.start_label = "NESTED_STATE_2";
        
        cout << "  Translating overlap section with SPMASK into TB" << tb.tb_id << ":" << endl;
        for (size_t i = 10; i < guest_code.size(); i++) {
            tb.packets.push_back(guest_code[i]);
            cout << "    EP" << guest_code[i].ep_num << ": ";
            for (const auto& insn : guest_code[i].instructions) {
                if (insn.parallel) cout << "|| ";
                if (!insn.predicate.empty()) cout << insn.predicate << " ";
                cout << insn.mnemonic;
                if (!insn.operands.empty()) cout << " " << insn.operands;
                cout << " ";
            }
            cout << endl;
        }
        
        cout << "  Generated TB" << tb.tb_id << " (overlap section with " << tb.packets.size() << " EPs)" << endl;
        return tb;
    }

    int getCyclesFromPrecedingTB() {
        if (saved_contexts.empty()) {
            return 1000;
        }
        
        int min_delay = saved_contexts[0].remaining_delay;
        for (const auto& ctx : saved_contexts) {
            if (ctx.remaining_delay < min_delay) {
                min_delay = ctx.remaining_delay;
            }
        }
        
        saved_contexts.erase(
            remove_if(saved_contexts.begin(), saved_contexts.end(),
                     [](const SavedContext& ctx) { return ctx.remaining_delay <= 0; }),
            saved_contexts.end()
        );
        
        return min_delay;
    }

    int getNextStartEP() {
        if (translation_blocks.empty()) {
            cout << "  No previous TB, starting from EP1 (index 0)" << endl;
            return 0;
        }
        
        TranslationBlock last_tb = translation_blocks.back();
        int next_start = last_tb.end_ep_index + 1;
        
        cout << "  Last TB (TB" << last_tb.tb_id << ") ended at EP" 
             << (last_tb.end_ep_index + 1) << endl;
        cout << "  Next TB will start at EP" << (next_start + 1) 
             << " (index " << next_start << ")" << endl;
        
        return next_start;
    }

    void simulateExecution() {
        cout << "\n======================================" << endl;
        cout << "VLIW DBT COMPLETE SIMULATION" << endl;
        cout << "======================================\n" << endl;
        
        cout << "\n********** PART 1: Figure 1 Assembly Code **********\n" << endl;
        parseGuestCode();
        
        cout << "Parsed " << guest_code.size() << " Execute Packets from Figure 1" << endl;
        
        cout << "\n--- Translating TB0 ---" << endl;
        int start_ep_tb1 = getNextStartEP();
        int initial_cycles = 1000;
        TranslationBlock tb1 = translateWithConstraint(start_ep_tb1, initial_cycles);
        translation_blocks.push_back(tb1);
        
        cout << "\n--- After TB0 execution ---" << endl;
        int cycles_for_tb2 = getCyclesFromPrecedingTB();
        cout << "Minimum remaining delay from TB0: " << cycles_for_tb2 << " cycles" << endl;
        
        cout << "\n--- Translating TB1 ---" << endl;
        int start_ep_tb2 = getNextStartEP();
        TranslationBlock tb2 = translateWithConstraint(start_ep_tb2, cycles_for_tb2);
        translation_blocks.push_back(tb2);


        
       
        cout << "\n--- After TB1 execution ---" << endl;
        int cycles_for_tb3 = getCyclesFromPrecedingTB();
        cout << "Minimum remaining delay from TB1: " << cycles_for_tb3 << " cycles" << endl;
            
        int next_ep_index = getNextStartEP(); 
        if (next_ep_index < guest_code.size()) {
                cout << "\n--- Translating TB2 (remaining instructions) ---" << endl;
                TranslationBlock tb3 = translateWithConstraint(next_ep_index, cycles_for_tb3);
                translation_blocks.push_back(tb3);
        } else {
                cout << "\n--- No more EPs to translate ---" << endl;
        }
        
        
        cout << "\n\n********** PART 2: Parallel LD/ST Handling (Figure 3) **********\n" << endl;
        ExecutePacket parallel_ep;
        parallel_ep.ep_num = 1;
        parallel_ep.cycles = 1;
        
        Instruction stw;
        stw.type = STORE;
        stw.mnemonic = "STW";
        stw.unit = ".D2";
        stw.operands = "B2, *B0++";
        stw.line_num = 1;
        stw.parallel = false;
        
        Instruction ldw;
        ldw.type = LOAD;
        ldw.mnemonic = "LDW";
        ldw.unit = ".D1";
        ldw.operands = "*A1++, A2";
        ldw.line_num = 2;
        ldw.parallel = true;
        
        parallel_ep.instructions.push_back(stw);
        parallel_ep.instructions.push_back(ldw);
        
        translateEPWithDeferredStores(parallel_ep);
        
        cout << "\n\n********** PART 3: Software-Pipelined Loop (Figure 4) **********\n" << endl;
        parseSoftwarePipelinedLoop();
        
        cout << "Parsed software-pipelined loop with " << guest_code.size() << " EPs" << endl;
        cout << "\nLoop structure:" << endl;
        
        for (size_t i = 0; i < guest_code.size(); i++) {
            const auto& ep = guest_code[i];
            cout << "EP" << ep.ep_num << " (line " << ep.instructions[0].line_num << "): ";
            for (const auto& insn : ep.instructions) {
                if (insn.parallel) cout << "|| ";
                cout << insn.mnemonic << " " << insn.operands << " ";
            }
            cout << endl;
        }
        
        ILC = 8;
        state = 0;
        
        cout << "\nSimulating loop with ILC=" << ILC << " iterations" << endl;
        
        // Call ONCE - it will handle all iterations internally
        translateSoftwarePipelinedLoop();
        
        cout << "\n\n********** PART 4: Nested Software-Pipelined Loop (Figure 6) **********\n" << endl;
        parseNestedSoftwarePipelinedLoop();
        
        cout << "Parsed nested loop with " << guest_code.size() << " EPs" << endl;
        
        cout << "\n=== Complete Instruction Body ===" << endl;
        for (size_t i = 0; i < guest_code.size(); i++) {
            const auto& ep = guest_code[i];
            cout << "EP" << ep.ep_num << " (cycles=" << ep.cycles << "):" << endl;
            for (const auto& insn : ep.instructions) {
                cout << "  ";
                if (insn.parallel) cout << "|| ";
                if (!insn.predicate.empty()) cout << insn.predicate << " ";
                cout << insn.mnemonic;
                if (!insn.unit.empty()) cout << " " << insn.unit;
                if (!insn.operands.empty()) cout << " " << insn.operands;
                cout << " (line " << insn.line_num << ")";
                if (insn.type == SPLOOP) cout << " [SPLOOP]";
                if (insn.type == SPKERNEL) cout << " [SPKERNEL]";
                if (insn.type == SPMASK) cout << " [SPMASK]";
                if (insn.type == BRANCH) cout << " [BRANCH, delay=" << insn.delay_slots << "]";
                if (insn.type == LOAD) cout << " [LOAD]";
                if (insn.type == STORE) cout << " [STORE]";
                cout << endl;
            }
        }
        cout << "=== End of Instruction Body ===\n" << endl;
        
        
        // Initialize nested loop parameters
        ILC = 7;    // From line 1: MVK .S 7, A8 -> MVC .S A8, ILC
        RILC = 7;   // From line 3: MVC .S A8, RILC
        A1 = 3;     // Set to 3 for demonstration of nested loop with overlap (originally MVK .S 1, A1 but we need multiple outer iterations)
        state = 0;
        
        cout << "\nInitial values: ILC=" << ILC << ", RILC=" << RILC << ", A1=" << A1 << endl;
        cout << "Note: A1 set to 3 (instead of 1) to demonstrate nested loop overlap section (EP12-EP15)" << endl;
        cout << "\nSimulating nested loop with proper state transitions:" << endl;
        
        // Call once per outer iteration - each call handles all inner iterations
        const int total_outer_iterations = 3;
        for (int outer_iteration = 1; outer_iteration <= total_outer_iterations; outer_iteration++) {
            cout << "\n========== OUTER ITERATION " << outer_iteration << " ==========\n" << endl;
            translateNestedLoop();
            
            if (state == 0 && A1 == 0) {
                break; // All loops completed
            }
        }
        
        cout << "\n========== Nested Loop Simulation Complete ==========\n" << endl;
        cout << "Total Translation Blocks generated: " << translation_blocks.size() << endl;
    }
};

int main() {
    VLIWSimulator simulator;
    simulator.simulateExecution();
    
    return 0;
}
