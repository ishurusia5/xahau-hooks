#include "Enum.h"
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using GuardLog =
    std::optional<std::reference_wrapper<std::basic_ostream<char>>>;

#define DEBUG_GUARD 0
#define DEBUG_GUARD_VERBOSE 0
#define DEBUG_GUARD_VERY_VERBOSE 0

#define GUARDLOG(logCode)                                                    \
    if (!guardLog)                                                           \
    {                                                                        \
    }                                                                        \
    else                                                                     \
        (*guardLog).get() << "HookSet(" << logCode << ")[" << guardLogAccStr \
                          << "]: "

// web assembly contains a lot of run length encoding in LEB128 format
inline uint64_t
parseLeb128(
    std::vector<unsigned char> const& buf,
    int start_offset,
    int* end_offset)
{
    uint64_t val = 0, shift = 0, i = start_offset;
    while (i < buf.size())
    {
        uint64_t b = (uint64_t)(buf[i]);
        uint64_t last = val;
        val += (b & 0x7FU) << shift;
        if (val < last)
        {
            // overflow
            throw std::overflow_error{"leb128 overflow"};
        }
        ++i;
        if (b & 0x80U)
        {
            shift += 7;
            if (!(i < buf.size()))
                throw std::length_error{"leb128 short or invalid"};
            continue;
        }
        *end_offset = i;

        return val;
    }
    return 0;
}

inline int64_t
parseSignedLeb128(
    std::vector<unsigned char> const& buf,
    int start_offset,
    int* end_offset)
{
    int64_t val = 0;
    uint64_t shift = 0, i = start_offset;
    while (i < buf.size())
    {
        uint64_t b = (uint64_t)(buf[i]);
        int64_t last = val;
        val += (b & 0x7FU) << shift;
        if (val < last)
        {
            // overflow
            throw std::overflow_error{"leb128 overflow"};
        }
        ++i;
        if (b & 0x80U)
        {
            shift += 7;
            if (!(i < buf.size()))
                throw std::length_error{"leb128 short or invalid"};
            continue;
        }
        *end_offset = i;
        if (shift < 64 && (b & 0x40U))
            val |= (~0 << shift);

        return val;
    }
    return 0;
}

// this macro will return temMALFORMED if i ever exceeds the end of the hook
#define CHECK_SHORT_HOOK()                                               \
    {                                                                    \
        if (i >= wasm.size())                                            \
        {                                                                \
            GUARDLOG(hook::log::SHORT_HOOK)                              \
                << "Malformed transaction: Hook truncated or otherwise " \
                   "invalid. "                                           \
                << "SetHook.cpp:" << __LINE__ << "\n";                   \
            return {};                                                   \
        }                                                                \
    }

#define REQUIRE(x)                                                       \
    {                                                                    \
        if (i + (x) > wasm.size())                                       \
        {                                                                \
            GUARDLOG(hook::log::SHORT_HOOK)                              \
                << "Malformed transaction: Hook truncated or otherwise " \
                   "invalid. "                                           \
                << "SetHook.cpp:" << __LINE__ << "\n";                   \
            return {};                                                   \
        }                                                                \
    }

#define ADVANCE(x) \
    {              \
        i += (x);  \
    }

#define LEB() parseLeb128(wasm, i, &i)

#define SIGNED_LEB() parseSignedLeb128(wasm, i, &i)

#define GUARD_ERROR(msg)                                                      \
    {                                                                         \
        char hex[64];                                                         \
        hex[0] = '\0';                                                        \
        snprintf(hex, 64, "%x", i);                                           \
        GUARDLOG(hook::log::GUARD_MISSING)                                    \
            << "GuardCheck " << (msg) << " "                                  \
            << "codesec: " << codesec << " hook byte offset: " << i << " [0x" \
            << hex << "]\n";                                                  \
        return {};                                                            \
    }

struct WasmBlkInf
{
    uint32_t sanity_check;
    uint32_t iteration_bound;
    uint32_t instruction_count;
    WasmBlkInf* parent;
    std::vector<WasmBlkInf*> children;
    uint32_t start_byte;
    bool is_root;

    WasmBlkInf(
        uint32_t iteration_bound_,
        uint32_t instruction_count_,
        WasmBlkInf* parent_,
        uint32_t start_byte_,
        bool is_root_ = false)
        : sanity_check(0x1234ABCDU)
        , iteration_bound(iteration_bound_)
        , instruction_count(instruction_count_)
        , parent(parent_)
        , children({})
        , start_byte(start_byte_)
        , is_root(is_root_)
    {
        // all done by the above
    }

    WasmBlkInf*
    add_child(uint32_t iteration_bound, uint32_t start_byte)
    {
        WasmBlkInf* child =
            new WasmBlkInf(iteration_bound, 0, this, start_byte, false);
        children.push_back(child);
        return child;
    }

    ~WasmBlkInf()
    {
        for (WasmBlkInf* child : children)
            delete child;
    }
};

#define PRINT_WCE(x)                                                           \
    {                                                                          \
        if (DEBUG_GUARD)                                                       \
            printf(                                                            \
                "%llx:: [%u]%.*swce=%ld | start=%x instcount=%u guard=%u, "    \
                "parent_guard=%d, multiplier=%g parentptr=%llx\n",             \
                &blk,                                                          \
                x,                                                             \
                level,                                                         \
                "                                                            " \
                "                      ",                                      \
                worst_case_execution,                                          \
                blk->start_byte,                                               \
                blk->instruction_count,                                        \
                blk->iteration_bound,                                          \
                (blk->parent != 0 ? blk->parent->iteration_bound : -1),        \
                multiplier,                                                    \
                &(blk->parent));                                               \
    }
// compute worst case execution time
inline uint64_t
compute_wce(const WasmBlkInf* blk, int level, bool* recursion_limit_reached)
{
    if (level > 16)
    {
        *recursion_limit_reached = true;
        return 0;
    }

    if (blk->sanity_check != 0x1234ABCDU)
    {
        printf("!!! sanity check failed\n");
        *recursion_limit_reached = true;
        return (uint64_t)-1;
    }

    WasmBlkInf const* parent = blk->parent;

    if (parent && parent->sanity_check != 0x1234ABCDU)
    {
        printf("!!! parent sanity check failed\n");
        *recursion_limit_reached = true;
        return (uint64_t)-1;
    }

    uint64_t worst_case_execution = blk->instruction_count;
    double multiplier = 1.0;

    if (blk->children.size() > 0)
        for (auto const& child : blk->children)
            worst_case_execution +=
                compute_wce(child, level + 1, recursion_limit_reached);

    if (parent == 0 ||
        parent->iteration_bound ==
            0)  // this condtion should never occur [defensively programmed]
    {
        PRINT_WCE(1);
        return worst_case_execution;
    }

    // if the block has a parent then the quotient of its guard and its parent's
    // guard gives us the loop iterations and thus the multiplier for the
    // instruction count
    multiplier =
        ((double)(blk->iteration_bound)) / ((double)(parent->iteration_bound));

    worst_case_execution *= multiplier;
    if (worst_case_execution < 1.0)
        worst_case_execution = 1.0;

    PRINT_WCE(3);
    return worst_case_execution;
};

// checks the WASM binary for the appropriate required _g guard calls and
// rejects it if they are not found start_offset is where the codesection or
// expr under analysis begins and end_offset is where it ends returns {worst
// case instruction count} if valid or {} if invalid may throw overflow_error,
// length_error
inline std::optional<uint64_t>
check_guard(
    std::vector<uint8_t> const& wasm,
    int codesec,
    int start_offset,
    int end_offset,
    int guard_func_idx,
    int last_import_idx,
    GuardLog guardLog,
    std::string guardLogAccStr,
    /* RH NOTE:
     * rules version is a bit field, so rule update 1 is 0x01, update 2 is 0x02
     * and update 3 is 0x04 ideally at rule version 3 all bits so far are set
     * (0b111) so the ruleVersion = 7, however if a specific rule update must be
     * rolled back due to unforeseen behaviour then this may no longer be the
     * case. using a bit field here leaves us flexible to rollback changes that
     * might have unforeseen consequences, without also rolling back further
     * changes that are fine.
     */
    uint64_t rulesVersion = 0

)
{
#define MAX_GUARD_CALLS 1024
    uint32_t guard_count = 0;

    if (DEBUG_GUARD)
        printf(
            "\ncheck_guard called with "
            "codesec=%d start_offset=%d end_offset=%d guard_func_idx=%d "
            "last_import_idx=%d\n",
            codesec,
            start_offset,
            end_offset,
            guard_func_idx,
            last_import_idx);

    if (end_offset <= 0)
        end_offset = wasm.size();
    int block_depth = 0;

    // the root node is constructed in a unique ptr, which will cause its
    // destructor to be called when the function exits. The destructor of the
    // root node will recursively free all heap allocated children.
    // WasmBlkInf(uint32_t iteration_bound_, uint32_t instruction_count_,
    //        WasmBlkInf* parent_, uint32_t start_byte_, bool is_root_ = false)
    //        :
    std::unique_ptr<WasmBlkInf> root =
        std::make_unique<WasmBlkInf>(1, 0, (WasmBlkInf*)0, start_offset, true);

    WasmBlkInf* current = &(*root);

    if (DEBUG_GUARD)
        printf("\n\n\nstart of guard analysis for codesec %d\n", codesec);

    for (int i = start_offset; i < end_offset;)
    {
        if (DEBUG_GUARD_VERY_VERBOSE)
        {
            printf("->");
            for (int z = i; z < 16 + i && z < end_offset; ++z)
                printf("%02X", wasm[z]);
            printf("\n");
        }

        REQUIRE(1);
        uint8_t instr = wasm[i];
        ADVANCE(1);

        current->instruction_count++;

        // unreachable and nop instructions
        if (instr == 0x00U ||  // unreachable
            instr == 0x01U ||  // nop
            instr == 0x05U)    // else
            continue;

        if (instr == 0x02U ||  // block
            instr == 0x03U ||  // loop
            instr == 0x04U)    // if
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "%s instruction at %d [%x]\n",
                    (instr == 0x02U ? "Block"
                                    : (instr == 0x03U ? "Loop" : "If")),
                    i,
                    i);

            // there must be at least a one byte block return type here
            REQUIRE(1);

            // discard the block return type
            uint8_t block_type = wasm[i];
            if ((block_type >= 0x7CU && block_type <= 0x7FU) ||
                block_type == 0x7BU || block_type == 0x70U ||
                block_type == 0x7BU || block_type == 0x40U ||
                block_type == 0x6FU)
            {
                ADVANCE(1);
            }
            else
            {
                SIGNED_LEB();
            }

            uint32_t iteration_bound =
                (current->parent == 0 ? 1 : current->iteration_bound);
            if (instr == 0x03U)
            {
                // now look for the guard call
                // this comprises 3 web assembly instructions, as per below
                // example 0001d8: 41 81 80 90 01             |   i32.const
                // 2359297 0001dd: 41 15                      |   i32.const 21
                // 0001df: 10 06                      |   call 6 <env._g>

                // first i32
                REQUIRE(1);
                if (wasm[i] != 0x41U)
                    GUARD_ERROR(
                        "Missing first i32.const after loop instruction");
                ADVANCE(1);
                SIGNED_LEB();  // this is the ID, we don't need it here

                // second i32
                REQUIRE(1);
                if (wasm[i] != 0x41U)
                    GUARD_ERROR(
                        "Missing second i32.const after loop instruction");
                ADVANCE(1);
                iteration_bound = LEB();  // second param is the iteration
                                          // bound, which is important here

                // guard call
                REQUIRE(1);
                if (wasm[i] != 0x10U)
                    GUARD_ERROR(
                        "Missing call to _g after first and second i32.const "
                        "at loop start");
                ADVANCE(1);
                uint64_t call_func_idx = LEB();  // the function being called
                                                 // *must* be the _g function

                if (iteration_bound == 0)
                    GUARD_ERROR("Guard call cannot specify 0 maxiter.");

                if (call_func_idx != guard_func_idx)
                    GUARD_ERROR(
                        "Call after first and second i32.const at loop start "
                        "was not _g");

                if (guard_count++ > MAX_GUARD_CALLS)
                    GUARD_ERROR("Too many guard calls! Limit is 1024");
            }

            current = current->add_child(iteration_bound, i);
            block_depth++;
            continue;
        }

        if (instr == 0x0BU)  // block end
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - block end instruction at %d [%x]\n", i, i);

            block_depth--;
            current = current->parent;
            if (current == 0 && block_depth == -1 && (i >= end_offset))
                break;  // codesec end
            else if (current == 0)
            {
                GUARD_ERROR("Illegal block end (current==0)");
            }
            else if (block_depth < 0)
            {
                GUARD_ERROR("Illegal block end (block_depth<0)");
            }

            if (current->sanity_check != 0x1234ABCDU)
            {
                GUARD_ERROR("Sanity check failed (bad pointer)");
            }
            continue;
        }

        if (instr == 0x0CU ||  // br
            instr == 0x0DU)    // br_if
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - %s instruction at %d [%x]\n",
                    (instr == 0x0CU ? "br" : "br_if"),
                    i,
                    i);

            REQUIRE(1);
            LEB();
            continue;
        }

        if (instr == 0x0EU)  // br_table
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - br_table instruction at %d [%x]\n", i, i);

            int vec_count = LEB();
            for (int v = 0; v < vec_count; ++v)
            {
                REQUIRE(1);
                LEB();
            }
            REQUIRE(1);
            LEB();
            continue;
        }

        if (instr == 0x0FU)  // return
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - return instruction at %d [%x]\n", i, i);
            continue;
        }

        if (instr == 0x10U)  // call
        {
            REQUIRE(1);
            uint64_t callee_idx = LEB();
            // disallow calling of user defined functions inside a hook
            if (callee_idx > last_import_idx)
            {
                GUARDLOG(hook::log::CALL_ILLEGAL)
                    << "GuardCheck "
                    << "Hook calls a function outside of the whitelisted "
                       "imports "
                    << "codesec: " << codesec << " hook byte offset: " << i
                    << "\n";

                return {};
            }

            // enforce guard call limit
            if (callee_idx == guard_func_idx)
            {
                if (guard_count++ > MAX_GUARD_CALLS)
                    GUARD_ERROR("Too many guard calls! Limit is 1024");
            }

            continue;
        }

        if (instr == 0x11U)  // call indirect
        {
            GUARDLOG(hook::log::CALL_INDIRECT)
                << "GuardCheck "
                << "Call indirect detected and is disallowed in hooks "
                << "codesec: " << codesec << " hook byte offset: " << i << "\n";
            return {};
        }

        // reference instructions
        if (instr >= 0xD0U && instr <= 0xD2)
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - reference instruction at %d [%x]\n", i, i);

            if (instr == 0x0D0U)
            {
                REQUIRE(1);
                // if it's a ref type it's a single byte
                if (!(wasm[i] == 0x70U || wasm[i] == 0x6FU))
                    GUARD_ERROR("Invalid reftype in 0xD0 instruction");
                ADVANCE(1);
            }
            else if (instr == 0x0D2U)
            {
                REQUIRE(1);
                LEB();
            }

            continue;
        }

        // parametric instructions
        if (instr == 0x1AU ||  // drop
            instr == 0x1BU ||  // select
            instr == 0x1CU)    // select t*
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - parametric instruction at %d [%x]\n",
                    i,
                    i);

            if (instr == 0x1CU)  // select t*
            {
                REQUIRE(1);
                uint64_t vec_count = LEB();
                for (uint64_t n = 0; n < vec_count; ++n)
                {
                    REQUIRE(1);
                    uint8_t v = wasm[i];
                    if ((v >= 0x7BU && v <= 0x7FU) || v == 0x70U || v == 0x6FU)
                    {
                        // fine
                    }
                    else
                        GUARD_ERROR("Invalid value type in select t* vector");
                    ADVANCE(1);
                }
            }
            continue;
        }

        // variable instructions
        if (instr >= 0x20U && instr <= 0x24U)
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - variable instruction at %d [%x]\n", i, i);

            REQUIRE(1);
            LEB();
            continue;
        }

        // table instructions + 0xFC instructions
        if (instr == 0x25U ||  // table.get
            instr == 0x26U ||  // table.set
            instr == 0xFCU)
        {
            REQUIRE(1);
            if (instr != 0xFCU)
            {
                if (DEBUG_GUARD_VERBOSE)
                    printf(
                        "Guard checker - table instruction at %d [%x]\n", i, i);
                LEB();
                continue;
            }

            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - 0xFC instruction at %d [%x]\n", i, i);

            uint64_t fc_type = LEB();
            REQUIRE(1);

            if (fc_type >= 12 && fc_type <= 17)  // table instructions
            {
                LEB();
                if (fc_type == 12 ||  // table.init
                    fc_type == 14)    // table.copy
                {
                    REQUIRE(1);
                    LEB();
                }
            }
            else if (fc_type == 8)  // memory.init
            {
                LEB();
                REQUIRE(1);
                ADVANCE(1);
            }
            else if (fc_type == 9)  // data.drop
            {
                LEB();
            }
            else if (fc_type == 10)  // memory.copy
            {
                if (rulesVersion & 0x02U)
                    GUARD_ERROR("Memory.copy instruction is not allowed.");

                REQUIRE(2);
                ADVANCE(2);
            }
            else if (fc_type == 11)  // memory.fill
            {
                if (rulesVersion & 0x02U)
                    GUARD_ERROR("Memory.fill instruction is not allowed.");

                ADVANCE(1);
            }
            else if (fc_type <= 7)  // numeric instructions
            {
                // do nothing, these have no parameters
            }
            else
                GUARD_ERROR("Illegal 0xFC instruction");

            continue;
        }

        // memory instructions
        if (instr >= 0x28U && instr <= 0x3EU)  // various loads and stores
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - memory instruction at %d [%x]\n", i, i);

            REQUIRE(1);
            LEB();
            REQUIRE(1);
            LEB();
            continue;
        }

        // more memory instructions
        if (instr == 0x3FU || instr == 0x40U)
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - memory instruction 2 at %d [%x]\n", i, i);

            REQUIRE(1);

            if (instr == 0x40U)  // disallow memory.grow
            {
                GUARDLOG(hook::log::MEMORY_GROW)
                    << "GuardCheck "
                    << "Memory.grow instruction not allowed at "
                    << "codesec: " << codesec << " hook byte offset: " << i
                    << "\n";
                return {};
            }

            ADVANCE(1);
            continue;
        }

        // numeric instructions (i32, i64)
        if (instr == 0x41U || instr == 0x42U)  // i32/64.const
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - i.const at %d [%x]\n", i, i);
            REQUIRE(1);
            LEB();
            continue;
        }

        // more numeric instructions
        if (instr == 0x43U)  // f32.const
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - f32.const at %d [%x]\n", i, i);

            REQUIRE(4);
            ADVANCE(4);
            continue;
        }

        if (instr == 0x44U)  // f64.const
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - f64.const at %d [%x]\n", i, i);

            REQUIRE(8);
            ADVANCE(8);
            continue;
        }

        // even more numeric instructions
        if (instr >= 0x45U && instr <= 0xC4U)
        {
            if (DEBUG_GUARD_VERBOSE)
                printf(
                    "Guard checker - numeric instruction at %d [%x]\n", i, i);

            // these have no arguments
            continue;
        }

        // vector instructions
        if (instr == 0xFDU)
        {
            if (DEBUG_GUARD_VERBOSE)
                printf("Guard checker - vector instruction at %d [%x]\n", i, i);

            REQUIRE(1);
            uint64_t v = LEB();

            if (v <= 11)  // memargs only
            {
                REQUIRE(1);
                LEB();
                REQUIRE(1);
                LEB();
            }
            else if (v >= 84U && v <= 91U)  // memargs + laneidx (1b)
            {
                REQUIRE(1);
                LEB();
                REQUIRE(1);
                LEB();
                REQUIRE(1);
                ADVANCE(1);
            }
            else if (v >= 21U && v <= 34U)  // laneidx (1b)
            {
                REQUIRE(1);
                ADVANCE(1);
            }
            else if (v == 12U || v == 13U)
            {
                REQUIRE(16);
                ADVANCE(16);
            }
            else
            {
                // no params do nothing
            }
            continue;
        }

        // execution to here is an error, unknown instruction
        {
            char ihex[64];
            ihex[0] = '\0';
            snprintf(
                ihex, 64, "Unknown instruction opcode: %d [%x]", instr, instr);
            GUARD_ERROR(ihex);
        }
    }

    bool recursion_limit_reached = false;
    uint64_t wce = compute_wce(&(*root), 0, &recursion_limit_reached);
    if (recursion_limit_reached)
    {
        GUARDLOG(hook::log::NESTING_LIMIT)
            << "GuardCheck "
            << "Maximum allowable depth of blocks reached (16 levels). Flatten "
               "your loops and conditions!.\n";
        return {};
    }

    GUARDLOG(hook::log::INSTRUCTION_COUNT)
        << "GuardCheck "
        << "Total worse-case execution count: " << wce << "\n";

    if (wce >= 0xFFFFU)
    {
        GUARDLOG(hook::log::INSTRUCTION_EXCESS)
            << "GuardCheck "
            << "Maximum possible instructions exceed 65535, please make your "
               "hook smaller "
            << "or check your guards!"
            << "\n";
        return {};
    }
    return wce;
}

// RH TODO: reprogram this function to use REQUIRE/ADVANCE
// may throw overflow_error
inline std::optional<  // unpopulated means invalid
    std::pair<
        uint64_t,  // max instruction count for hook()
        uint64_t   // max instruction count for cbak()
        >>
validateGuards(
    std::vector<uint8_t> const& wasm,
    GuardLog guardLog,
    std::string guardLogAccStr,
    /* RH NOTE:
     * rules version is a bit field, so rule update 1 is 0x01, update 2 is 0x02
     * and update 3 is 0x04 ideally at rule version 3 all bits so far are set
     * (0b111) so the ruleVersion = 7, however if a specific rule update must be
     * rolled back due to unforeseen behaviour then this may no longer be the
     * case. using a bit field here leaves us flexible to rollback changes that
     * might have unforeseen consequences, without also rolling back further
     * changes that are fine.
     */
    uint64_t rulesVersion = 0)
{
    uint64_t byteCount = wasm.size();

    // 63 bytes is the smallest possible valid hook wasm
    if (byteCount < 63U)
    {
        GUARDLOG(hook::log::WASM_TOO_SMALL)
            << "Malformed transaction: Hook was not valid webassembly binary. "
               "Too small."
            << "\n";
        return {};
    }

    // check header, magic number
    unsigned char header[8] = {
        0x00U, 0x61U, 0x73U, 0x6DU, 0x01U, 0x00U, 0x00U, 0x00U};
    for (int i = 0; i < 8; ++i)
    {
        if (wasm[i] != header[i])
        {
            GUARDLOG(hook::log::WASM_BAD_MAGIC)
                << "Malformed transaction: Hook was not valid webassembly "
                   "binary. "
                << "Missing magic number or version."
                << "\n";
            return {};
        }
    }

    // these will store the function type indicies of hook and cbak if
    // hook and cbak are found in the export section
    std::optional<int> hook_func_idx;
    std::optional<int> cbak_func_idx;

    // this maps function ids to type ids, used for looking up the type of cbak
    // and hook as established inside the wasm binary.
    std::map<int, int> func_type_map;
    std::map<
        int /* type idx */,
        std::map<int /* import index */, std::string /* api name */>>
        import_type_map;

    // now we check for guards... first check if _g is imported
    int guard_import_number = -1;
    int last_import_number = -1;
    int import_count = 0;
    int last_section_type = 0;
    for (int i = 8, j = 0; i < wasm.size();)
    {
        if (j == i)
        {
            // if the loop iterates twice with the same value for i then
            // it's an infinite loop edge case
            GUARDLOG(hook::log::WASM_PARSE_LOOP)
                << "Malformed transaction: Hook is invalid WASM binary."
                << "\n";
            return {};
        }

        j = i;

        // each web assembly section begins with a single byte section type
        // followed by an leb128 length
        int section_type = wasm[i++];

        if (section_type == 0)
        {
            GUARDLOG(hook::log::CUSTOM_SECTION_DISALLOWED)
                << "Malformed transaction. "
                << "Hook contained a custom section, which is not allowed. Use "
                   "cleaner.\n";
            return {};
        }

        if (section_type <= last_section_type)
        {
            GUARDLOG(hook::log::SECTIONS_OUT_OF_SEQUENCE)
                << "Malformed transcation. "
                << "Hook contained wasm sections that were either repeated or "
                   "were out of sequence.\n";
            return {};
        }

        last_section_type = section_type;

        int section_length = parseLeb128(wasm, i, &i);
        CHECK_SHORT_HOOK();
        // int section_start = i;

        if (DEBUG_GUARD_VERBOSE)
            printf(
                "WASM binary analysis -- upto %d: section %d with length %d\n",
                i,
                section_type,
                section_length);

        int next_section = i + section_length;

        if (section_type == 2)  // import section
        {
            // we are interested in the import section... we need to know if _g
            // is imported and which import# it is
            import_count = parseLeb128(wasm, i, &i);
            CHECK_SHORT_HOOK();
            if (import_count <= 0)
            {
                GUARDLOG(hook::log::IMPORTS_MISSING)
                    << "Malformed transaction. "
                    << "Hook did not import any functions... "
                    << "required at least guard(uint32_t, uint32_t) and accept "
                       "or rollback"
                    << "\n";
                return {};
            }

            // process each import one by one
            int func_upto = 0;  // not all imports are functions so we need an
                                // indep counter for these
            for (int j = 0; j < import_count; ++j)
            {
                // first check module name
                int mod_length = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                if (mod_length < 1 || mod_length > (wasm.size() - i))
                {
                    GUARDLOG(hook::log::IMPORT_MODULE_BAD)
                        << "Malformed transaction. "
                        << "Hook attempted to specify nil or invalid import "
                           "module"
                        << "\n";
                    return {};
                }

                if (std::string_view(
                        (const char*)(wasm.data() + i), (size_t)mod_length) !=
                    "env")
                {
                    GUARDLOG(hook::log::IMPORT_MODULE_ENV)
                        << "Malformed transaction. "
                        << "Hook attempted to specify import module other than "
                           "'env'"
                        << "\n";
                    return {};
                }

                i += mod_length;
                CHECK_SHORT_HOOK();

                // next get import name
                int name_length = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                if (name_length < 1 || name_length > (wasm.size() - i))
                {
                    GUARDLOG(hook::log::IMPORT_NAME_BAD)
                        << "Malformed transaction. "
                        << "Hook attempted to specify nil or invalid import "
                           "name"
                        << "\n";
                    return {};
                }

                std::string import_name{
                    (const char*)(wasm.data() + i), (size_t)name_length};

                i += name_length;
                CHECK_SHORT_HOOK();

                // next get import type
                if (wasm[i] > 0x00)
                {
                    // not a function import
                    GUARDLOG(hook::log::IMPORT_ILLEGAL)
                        << "Malformed transaction. "
                        << "Hook attempted to import an import type other than "
                           "a function.\n";
                    return {};
                }

                // execution to here means it's a function import
                i++;
                CHECK_SHORT_HOOK();
                int type_idx = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();

                if (import_name == "_g")
                {
                    guard_import_number = func_upto;
                }
                else if (
                    hook_api::import_whitelist.find(import_name) ==
                    hook_api::import_whitelist.end())
                {
                    if (rulesVersion > 0 &&
                        hook_api::import_whitelist_1.find(import_name) !=
                            hook_api::import_whitelist_1.end())
                    {
                        // PASS, this is a version 1 api
                    }
                    else
                    {
                        GUARDLOG(hook::log::IMPORT_ILLEGAL)
                            << "Malformed transaction. "
                            << "Hook attempted to import a function that does "
                               "not "
                            << "appear in the hook_api function set: `"
                            << import_name << "`"
                            << "\n";
                        return {};
                    }
                }

                // add to import map
                if (import_type_map.find(type_idx) == import_type_map.end())
                    import_type_map[type_idx] = {
                        {func_upto, std::move(import_name)}};
                else
                    import_type_map[type_idx].emplace(
                        func_upto, std::move(import_name));

                func_upto++;
            }

            if (guard_import_number == -1)
            {
                GUARDLOG(hook::log::GUARD_IMPORT)
                    << "Malformed transaction. "
                    << "Hook did not import _g (guard) function"
                    << "\n";
                return {};
            }

            last_import_number = func_upto - 1;

            // we have an imported guard function, so now we need to enforce the
            // guard rule: all loops must start with a guard call before any
            // branching to enforce these rules we must do a second pass of the
            // wasm in case the function section was placed in this wasm binary
            // before the import section
        }
        else if (section_type == 7)  // export section
        {
            int export_count = parseLeb128(wasm, i, &i);
            CHECK_SHORT_HOOK();
            if (export_count <= 0)
            {
                GUARDLOG(hook::log::EXPORTS_MISSING)
                    << "Malformed transaction. "
                    << "Hook did not export any functions... "
                    << "required hook(int64_t), callback(int64_t)."
                    << "\n";
                return {};
            }

            for (int j = 0; j < export_count; ++j)
            {
                int name_len = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                if (name_len == 4)
                {
                    if (wasm[i] == 'h' && wasm[i + 1] == 'o' &&
                        wasm[i + 2] == 'o' && wasm[i + 3] == 'k')
                    {
                        i += name_len;
                        CHECK_SHORT_HOOK();
                        if (wasm[i] != 0)
                        {
                            GUARDLOG(hook::log::EXPORT_HOOK_FUNC)
                                << "Malformed transaction. "
                                << "Hook did not export: A valid int64_t "
                                   "hook(uint32_t)"
                                << "\n";
                            return {};
                        }

                        i++;
                        CHECK_SHORT_HOOK();
                        hook_func_idx = parseLeb128(wasm, i, &i);
                        CHECK_SHORT_HOOK();
                        continue;
                    }

                    if (wasm[i] == 'c' && wasm[i + 1] == 'b' &&
                        wasm[i + 2] == 'a' && wasm[i + 3] == 'k')
                    {
                        i += name_len;
                        CHECK_SHORT_HOOK();
                        if (wasm[i] != 0)
                        {
                            GUARDLOG(hook::log::EXPORT_CBAK_FUNC)
                                << "Malformed transaction. "
                                << "Hook did not export: A valid int64_t "
                                   "cbak(uint32_t)"
                                << "\n";
                            return {};
                        }
                        i++;
                        CHECK_SHORT_HOOK();
                        cbak_func_idx = parseLeb128(wasm, i, &i);
                        CHECK_SHORT_HOOK();
                        continue;
                    }
                }

                i += name_len + 1;
                parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
            }

            // execution to here means export section was parsed
            if (!hook_func_idx)
            {
                GUARDLOG(hook::log::EXPORT_MISSING)
                    << "Malformed transaction. "
                    << "Hook did not export: "
                    << (!hook_func_idx ? "int64_t hook(uint32_t); " : "")
                    << "\n";
                return {};
            }
        }
        else if (section_type == 3)  // function section
        {
            int function_count = parseLeb128(wasm, i, &i);
            CHECK_SHORT_HOOK();
            if (function_count <= 0)
            {
                GUARDLOG(hook::log::FUNCS_MISSING)
                    << "Malformed transaction. "
                    << "Hook did not establish any functions... "
                    << "required hook(int64_t), callback(int64_t)."
                    << "\n";
                return {};
            }

            for (int j = 0; j < function_count; ++j)
            {
                int type_idx = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                if (DEBUG_GUARD)
                    printf("Function map: func %d -> type %d\n", j, type_idx);
                func_type_map[j] = type_idx;
            }
        }

        i = next_section;
        continue;
    }

    // we must subtract import_count from the hook and cbak function in order to
    // be able to look them up in the functions section. this is a rule of the
    // webassembly spec note that at this point in execution we are guarenteed
    // these are populated
    *hook_func_idx -= import_count;

    if (cbak_func_idx)
        *cbak_func_idx -= import_count;

    if (func_type_map.find(*hook_func_idx) == func_type_map.end() ||
        (cbak_func_idx &&
         func_type_map.find(*cbak_func_idx) == func_type_map.end()))
    {
        GUARDLOG(hook::log::FUNC_TYPELESS)
            << "Malformed transaction. "
            << "hook or cbak functions did not have a corresponding type in "
               "WASM binary."
            << "\n";
        return {};
    }

    int hook_type_idx = func_type_map[*hook_func_idx];

    // cbak function is optional so if it exists it has a type otherwise it is
    // skipped in checks
    if (cbak_func_idx && func_type_map[*cbak_func_idx] != hook_type_idx)
    {
        GUARDLOG(hook::log::HOOK_CBAK_DIFF_TYPES)
            << "Malformed transaction. "
            << "Hook and cbak func must have the same type. int64_t "
               "(*)(uint32_t).\n";
        return {};
    }

    int64_t maxInstrCountHook = 0;
    int64_t maxInstrCountCbak = 0;

    // second pass... where we check all the guard function calls follow the
    // guard rules minimal other validation in this pass because first pass
    // caught most of it
    for (int i = 8; i < wasm.size();)
    {
        int section_type = wasm[i++];
        int section_length = parseLeb128(wasm, i, &i);
        CHECK_SHORT_HOOK();
        // int section_start = i;
        int next_section = i + section_length;

        if (section_type == 1)  // type section
        {
            int type_count = parseLeb128(wasm, i, &i);
            CHECK_SHORT_HOOK();
            for (int j = 0; j < type_count; ++j)
            {
                if (wasm[i++] != 0x60)
                {
                    GUARDLOG(hook::log::FUNC_TYPE_INVALID)
                        << "Invalid function type. "
                        << "Codesec: " << section_type << " "
                        << "Local: " << j << " "
                        << "Offset: " << i << "\n";
                    return {};
                }
                CHECK_SHORT_HOOK();

                // check the consistency of the type
                std::optional<std::string> first_name;
                std::optional<
                    std::reference_wrapper<std::vector<uint8_t> const>>
                    first_signature;
                if (auto const& usage = import_type_map.find(j);
                    usage != import_type_map.end())
                {
                    for (auto const& [import_idx, api_name] : usage->second)
                    {
                        auto const& api_signature =
                            hook_api::import_whitelist.find(api_name) !=
                                hook_api::import_whitelist.end()
                            ? hook_api::import_whitelist.find(api_name)->second
                            : hook_api::import_whitelist_1.find(api_name)
                                  ->second;

                        if (!first_signature)
                        {
                            first_name = api_name;
                            first_signature = api_signature;
                            continue;
                        }

                        if (api_signature != (*first_signature).get())
                        {
                            GUARDLOG(hook::log::FUNC_TYPE_INVALID)
                                << "Function type is inconsitent across "
                                   "referenced apis. "
                                << "This probably means one of your apis has "
                                   "the wrong signature. "
                                << "(Either: " << *first_name
                                << ", or: " << api_name << ".) "
                                << "Codesec: " << section_type << " "
                                << "Local: " << j << " "
                                << "Offset: " << i << "\n";
                            return {};
                        }
                    }
                }
                else if (j == hook_type_idx)
                {
                    // pass
                }
                else
                {
                    // fail
                    GUARDLOG(hook::log::FUNC_TYPE_INVALID)
                        << "Invalid function type. Not used by any import or "
                           "hook/cbak func. "
                        << "Codesec: " << section_type << " "
                        << "Local: " << j << " "
                        << "Offset: " << i << "\n";
                    return {};
                }

                int param_count = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                if (j == hook_type_idx)
                {
                    if (param_count != 1)
                    {
                        GUARDLOG(hook::log::PARAM_HOOK_CBAK)
                            << "Malformed transaction. "
                            << "hook and cbak function definition must have "
                               "exactly one parameter (uint32_t)."
                            << "\n";
                        return {};
                    }
                }
                else if (param_count != (*first_signature).get().size() - 1)
                {
                    GUARDLOG(hook::log::FUNC_TYPE_INVALID)
                        << "Malformed transaction. "
                        << "Hook API: " << *first_name
                        << " has the wrong number of parameters.\n";
                    return {};
                }

                for (int k = 0; k < param_count; ++k)
                {
                    int param_type = parseLeb128(wasm, i, &i);
                    CHECK_SHORT_HOOK();
                    if (param_type == 0x7FU || param_type == 0x7EU ||
                        param_type == 0x7DU || param_type == 0x7CU)
                    {
                        // pass, this is fine
                    }
                    else
                    {
                        GUARDLOG(hook::log::FUNC_PARAM_INVALID)
                            << "Invalid parameter type in function type. "
                            << "Codesec: " << section_type << " "
                            << "Local: " << j << " "
                            << "Offset: " << i << "\n";
                        return {};
                    }

                    if (DEBUG_GUARD)
                        printf(
                            "Function type idx: %d, hook_func_idx: %d, "
                            "cbak_func_idx: %d "
                            "param_count: %d param_type: %x\n",
                            j,
                            *hook_func_idx,
                            *cbak_func_idx,
                            param_count,
                            param_type);

                    // hook and cbak parameter check here
                    if (j == hook_type_idx)
                    {
                        if (param_type != 0x7FU /* i32 */)
                        {
                            GUARDLOG(hook::log::PARAM_HOOK_CBAK)
                                << "Malformed transaction. "
                                << "hook and cbak function definition must "
                                   "have exactly one uint32_t parameter."
                                << "\n";
                            return {};
                        }
                    }
                    else if ((*first_signature).get()[k + 1] != param_type)
                    {
                        GUARDLOG(hook::log::FUNC_PARAM_INVALID)
                            << "Malformed transaction. "
                            << "Hook API: " << *first_name
                            << " definition parameters incorrect."
                            << "\n";
                        return {};
                    }
                }

                int result_count = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();

                // this needs a reliable hook cleaner otherwise it will catch
                // most compilers out
                if (result_count != 1)
                {
                    GUARDLOG(hook::log::FUNC_RETURN_COUNT)
                        << "Malformed transaction. "
                        << "Hook declares a function type that returns fewer "
                           "or more than one value. "
                        << "\n";
                    return {};
                }

                // this can only ever be 1 in production, but in testing it may
                // also be 0 or >1 so for completeness this loop is here but can
                // be taken out in prod
                for (int k = 0; k < result_count; ++k)
                {
                    int result_type = parseLeb128(wasm, i, &i);
                    CHECK_SHORT_HOOK();
                    if (result_type == 0x7F || result_type == 0x7E ||
                        result_type == 0x7D || result_type == 0x7C)
                    {
                        // pass, this is fine
                    }
                    else
                    {
                        GUARDLOG(hook::log::FUNC_RETURN_INVALID)
                            << "Invalid return type in function type. "
                            << "Codesec: " << section_type << " "
                            << "Local: " << j << " "
                            << "Offset: " << i << "\n";
                        return {};
                    }

                    if (DEBUG_GUARD)
                        printf(
                            "Function type idx: %d, hook_func_idx: %d, "
                            "cbak_func_idx: %d "
                            "result_count: %d result_type: %x\n",
                            j,
                            *hook_func_idx,
                            *cbak_func_idx,
                            result_count,
                            result_type);

                    // hook and cbak return type check here
                    if (j == hook_type_idx)
                    {
                        if (result_count != 1 || result_type != 0x7E /* i64 */)
                        {
                            GUARDLOG(hook::log::RETURN_HOOK_CBAK)
                                << "Malformed transaction. "
                                << (j == hook_type_idx ? "hook" : "cbak")
                                << " j=" << j << " "
                                << " function definition must have exactly one "
                                   "int64_t return type. "
                                << "resultcount=" << result_count
                                << ", resulttype=" << result_type << ", "
                                << "paramcount=" << param_count << "\n";
                            return {};
                        }
                    }
                    else if ((*first_signature).get()[0] != result_type)
                    {
                        GUARDLOG(hook::log::FUNC_RETURN_INVALID)
                            << "Malformed transaction. "
                            << "Hook API: " << *first_name
                            << " definition return type incorrect."
                            << "\n";
                        return {};
                    }
                }
            }
        }
        else if (section_type == 10)  // code section
        {
            // RH TODO: parse anywhere else an expr is allowed in wasm and
            // enforce rules there too these are the functions
            int func_count = parseLeb128(wasm, i, &i);
            CHECK_SHORT_HOOK();

            for (int j = 0; j < func_count; ++j)
            {
                // parse locals
                int code_size = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                int code_end = i + code_size;
                int local_count = parseLeb128(wasm, i, &i);
                CHECK_SHORT_HOOK();
                for (int k = 0; k < local_count; ++k)
                {
                    /*int array_size = */
                    parseLeb128(wasm, i, &i);
                    CHECK_SHORT_HOOK();
                    if (!(wasm[i] >= 0x7C && wasm[i] <= 0x7F))
                    {
                        GUARDLOG(hook::log::TYPE_INVALID)
                            << "Invalid local type. "
                            << "Codesec: " << j << " "
                            << "Local: " << k << " "
                            << "Offset: " << i << "\n";
                        return {};
                    }
                    i++;
                    CHECK_SHORT_HOOK();
                }

                if (i == code_end)
                    continue;  // allow empty functions

                // execution to here means we are up to the actual expr for the
                // codesec/function

                auto valid = check_guard(
                    wasm,
                    j,
                    i,
                    code_end,
                    guard_import_number,
                    last_import_number,
                    guardLog,
                    guardLogAccStr,
                    rulesVersion);

                if (!valid)
                    return {};

                if (hook_func_idx && *hook_func_idx == j)
                    maxInstrCountHook = *valid;
                else if (cbak_func_idx && *cbak_func_idx == j)
                    maxInstrCountCbak = *valid;
                else
                {
                    if (DEBUG_GUARD)
                        printf(
                            "code section: %d not hook_func_idx: %d or "
                            "cbak_func_idx: %d\n",
                            j,
                            *hook_func_idx,
                            (cbak_func_idx ? *cbak_func_idx : -1));
                    //   assert(false);
                }
                i = code_end;
            }
        }
        i = next_section;
    }

    // execution to here means guards are installed correctly

    return std::pair<uint64_t, uint64_t>{maxInstrCountHook, maxInstrCountCbak};
}
