#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <ctype.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <strings.h>


#define MIPS_REGISTER_COUNT        32
#define MIPS_MAX_OPERANDS         3
#define MIPS_MAX_SOURCE_LINE      256
#define MIPS_MAX_CONFIG_LINE      512
#define MIPS_MAX_VMS              32
#define MIPS_WORD_SIZE            4

#define MIPS_REGISTER_ZERO        0
#define MIPS_REGISTER_HI          32
#define MIPS_REGISTER_LO          33

#define DEFAULT_TEXT_BASE         0x00400000u


//STRUCTS


typedef struct MIPSRegisters
{
    uint32_t gpr[MIPS_REGISTER_COUNT];
    uint32_t hi;
    uint32_t lo;
    uint32_t pc;
} MIPSRegisters;

typedef struct {
    char *paths[MIPS_MAX_VMS];
    bool is_snapshot[MIPS_MAX_VMS];
    size_t count;
    bool ok;
} ConfigPaths;

typedef struct VMConfig
{
    size_t vm_exec_slice_in_instructions;
    char vm_binary[PATH_MAX];
} VMConfig;


typedef enum OperandType
{
    OPERAND_NONE = 0,
    OPERAND_REGISTER,
    OPERAND_IMMEDIATE,
    OPERAND_LABEL
} OperandType;


typedef struct Operand
{
    OperandType type;

    union
    {
        unsigned int register_number;
        int32_t immediate;
        char label[MIPS_MAX_SOURCE_LINE];
    } value;
} Operand;

// AI used to speed up the process of writing the code by generating the Opcode enum from the PDF.
typedef enum Opcode
{
    OPCODE_INVALID = 0,
    OPCODE_NOOP,

    OPCODE_LI,
    OPCODE_DUMP_PROCESSOR_STATE,
    OPCODE_SNAPSHOT,

    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_ADDI,
    OPCODE_ADDU,
    OPCODE_SUBU,
    OPCODE_ADDIU,
    OPCODE_MUL,
    OPCODE_MULT,
    OPCODE_DIV,

    OPCODE_AND,
    OPCODE_OR,
    OPCODE_ANDI,
    OPCODE_XOR,
    OPCODE_SLL,
    OPCODE_SRL,

    OPCODE_LW,
    OPCODE_SW,
    OPCODE_LUI,
    OPCODE_LA,
    OPCODE_MFHI,
    OPCODE_MFLO,
    OPCODE_MOVE,

    OPCODE_BEQ,
    OPCODE_BNE,
    OPCODE_BGT,
    OPCODE_BGE,
    OPCODE_BLT,
    OPCODE_BLE,

    OPCODE_SLT,
    OPCODE_SLTI,

    OPCODE_J,
    OPCODE_JR,
    OPCODE_JAL,

    OPCODE_SYSCALL
} Opcode;


typedef struct {
    const char *name;
    Opcode opcode;
} OpcodeMap;

// AI used to speed up the process of writing the code by generating the OpcodeMap struct and OPCODE_TABLE from the PDF.
static const OpcodeMap OPCODE_TABLE[] =
{
    { "li",                    OPCODE_LI },
    { "DUMP_PROCESSOR_STATE",  OPCODE_DUMP_PROCESSOR_STATE },
    { "snapshot",              OPCODE_SNAPSHOT },

    { "add",                   OPCODE_ADD },
    { "sub",                   OPCODE_SUB },
    { "addi",                  OPCODE_ADDI },
    { "addu",                  OPCODE_ADDU },
    { "subu",                  OPCODE_SUBU },
    { "addiu",                 OPCODE_ADDIU },
    { "mul",                   OPCODE_MUL },
    { "mult",                  OPCODE_MULT },
    { "div",                   OPCODE_DIV },

    { "and",                   OPCODE_AND },
    { "or",                    OPCODE_OR },
    { "andi",                  OPCODE_ANDI },
    { "xor",                   OPCODE_XOR },
    { "sll",                   OPCODE_SLL },
    { "srl",                   OPCODE_SRL },

    { "lw",                    OPCODE_LW },
    { "sw",                    OPCODE_SW },
    { "lui",                   OPCODE_LUI },
    { "la",                    OPCODE_LA },
    { "mfhi",                  OPCODE_MFHI },
    { "mflo",                  OPCODE_MFLO },
    { "move",                  OPCODE_MOVE },

    { "beq",                   OPCODE_BEQ },
    { "bne",                   OPCODE_BNE },
    { "bgt",                   OPCODE_BGT },
    { "bge",                   OPCODE_BGE },
    { "blt",                   OPCODE_BLT },
    { "ble",                   OPCODE_BLE },

    { "slt",                   OPCODE_SLT },
    { "slti",                  OPCODE_SLTI },

    { "j",                     OPCODE_J },
    { "jr",                    OPCODE_JR },
    { "jal",                   OPCODE_JAL },

    { "syscall",               OPCODE_SYSCALL }
};
#define OPCODE_TABLE_SIZE (sizeof(OPCODE_TABLE) / sizeof(OPCODE_TABLE[0]))

typedef struct MIPSInstruction
{
    Opcode opcode;

    Operand operands[MIPS_MAX_OPERANDS];
    size_t operand_count;

    uint32_t address;
    size_t source_line_number;
} MIPSInstruction;


typedef struct MIPSProgram
{
    MIPSInstruction *instructions;

    size_t instruction_count;
    size_t instruction_capacity;

    uint32_t text_base;
} MIPSProgram;


typedef struct VM
{
    unsigned int vm_id;
    VMConfig config;

    MIPSRegisters registers;
    MIPSProgram program;
    size_t current_instruction_index;
    bool halted;
} VM;


typedef struct Hypervisor
{
    VM vms[MIPS_MAX_VMS];

    size_t vm_count;
    size_t vm_capacity;

    size_t current_vm_index;
} Hypervisor;

//UTILS
int path_exists(const char *path) {
 return access(path, F_OK) == 0;
}

char *trim_whitespace(char *str) {
    char *end;

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == 0) {
        return str;
    }

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    end[1] = '\0';

    return str;
}


//MAIN

bool parse_execution_slice(const char *value, size_t *slice) {
    if (value == NULL || *value == '\0') {
        return false;
    }

    char *endptr;
    long parsed_value = strtol(value, &endptr, 10);

    if (*endptr != '\0' || parsed_value < 0) {
        return false;
    }

    if (parsed_value == 0) {
        return false;
    }

    *slice = (size_t)parsed_value;
    return true;
}


int parse_config_file(const char *config_path, VMConfig *config) {
    FILE *file = fopen(config_path, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open configuration file '%s'.\n", config_path);
        return -1;
    }
 char buffer[MIPS_MAX_CONFIG_LINE];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        char *line = trim_whitespace(buffer);
        if (line[0] == '\0') {
            continue;
        }

        char *equals_sign = strchr(line, '=');
        if (!equals_sign) {
            fprintf(stderr, "Error: Invalid configuration line: '%s'\n", line);
            fclose(file);
            return -1;
        }

        *equals_sign = '\0';
        char *param_name = trim_whitespace(line);
        char *param_value = trim_whitespace(equals_sign + 1);

        if (strcmp(param_name, "vm_exec_slice_in_instructions") == 0) {
            parse_execution_slice(param_value, &config->vm_exec_slice_in_instructions);
            config->vm_exec_slice_in_instructions = strtoul(param_value, NULL, 0);
        } else if (strcmp(param_name, "vm_binary") == 0) {
            strncpy(config->vm_binary, param_value, PATH_MAX - 1);
        } else {
            fprintf(stderr, "Warning: Unknown configuration parameter: '%s'\n", param_name);
        }
    }

    fclose(file);
    return 0;
}


ConfigPaths parse_command_line(int argc, char **argv) {
   ConfigPaths result = { .count = 0, .ok = true };
   int opt;

   while ((opt = getopt(argc, argv, "v:s:")) != -1) {
      if (opt == 'v' || opt == 's') {
           if (result.count >= MIPS_MAX_VMS) {
               fprintf(stderr, "Error: Exceeded maximum VM files limit (%d).\n", MIPS_MAX_VMS);
                result.ok = false;
                return result;
           }
            if (!path_exists(optarg)) {
               fprintf(stderr, "Error: VM file '%s' does not exist.\n", optarg);
                result.ok = false;
                return result;
            }
            result.paths[result.count++] = optarg;
           result.is_snapshot[result.count - 1] = (opt == 's');
      }
      else {
          fprintf(stderr, "Usage: %s [-v <config_file> | -s <snapshot>] ...\n", argv[0]);
          result.ok = false;
          return result;
      }
    }
    return result;
}


void initialize_registers(MIPSRegisters *registers) {
    for (unsigned int i = 0; i < MIPS_REGISTER_COUNT; ++i) {
        registers->gpr[i] = 0;
    }
    registers->hi = 0;
    registers->lo = 0;
    registers->pc = DEFAULT_TEXT_BASE;
}


uint32_t read_register(const MIPSRegisters *registers,
                       unsigned int register_number) {
    if (register_number >= MIPS_REGISTER_COUNT) {
        fprintf(stderr, "Error: Invalid register number %u. Must be between 0 and 31.\n", register_number);
        return 0;
    }
    if (register_number == 0) {
        return 0;
    }
    return registers->gpr[register_number];
}


void write_register(MIPSRegisters *registers,
                    unsigned int register_number,
                    uint32_t value) {
    if (register_number != 0) {
        registers->gpr[register_number] = value;
    }
}

// AI assisted to speed up the process of writing the code by generating the parse_register_name function from the PDF.
unsigned int parse_register_name(const char *token) {
    static const char *const REGISTER_NAMES[MIPS_REGISTER_COUNT] =
    {
        "$zero",

        "$1",
        "$v0",
        "$v1",

        "$a0",
        "$a1",
        "$a2",
        "$a3",

        "$t0",
        "$t1",
        "$t2",
        "$t3",
        "$t4",
        "$t5",
        "$t6",
        "$t7",

        "$s0",
        "$s1",
        "$s2",
        "$s3",
        "$s4",
        "$s5",
        "$s6",
        "$s7",

        "$t8",
        "$t9",

        "$26",
        "$27",

        "$28",
        "$29",
        "$30",

        "$ra"
    };

    char *end;
    unsigned long value;
    unsigned int i;

    if (token == NULL)
        return MIPS_REGISTER_COUNT;


    for (i = 0; i < MIPS_REGISTER_COUNT; ++i)
    {
        if (strcmp(token, REGISTER_NAMES[i]) == 0)
            return i;
    }

    if (token[0] != '$' || token[1] == '\0')
        return MIPS_REGISTER_COUNT;

    value = strtoul(token + 1, &end, 10);

    if (*end != '\0' || value >= MIPS_REGISTER_COUNT)
        return MIPS_REGISTER_COUNT;

    return (unsigned int)value;
}


bool is_integer(const char *str, int32_t *out_val) {
    if (!str || *str == '\0') return false;

    char *endptr;
    long val = strtol(str, &endptr, 0);

    if (endptr != str && *endptr == '\0') {
        if (out_val) *out_val = val;
        return true;
    }
    return false;
}

Operand parse_operand(const char *token) {
    Operand result;
    if (token == NULL || *token == '\0') {
         result.type = OPERAND_NONE;
    }
    else if (token[0] == '$') {
        result.type = OPERAND_REGISTER;
        unsigned int regNum = parse_register_name(token);
        if (regNum >= MIPS_REGISTER_COUNT) {
            result.type = OPERAND_NONE;
        } else {
            result.value.register_number = regNum;
        }
    }
    else if (is_integer(token, &result.value.immediate)) {
        result.type = OPERAND_IMMEDIATE;
    }
    else {
        result.type = OPERAND_LABEL;
        strncpy(result.value.label, token, MIPS_MAX_SOURCE_LINE - 1);
        result.value.label[MIPS_MAX_SOURCE_LINE - 1] = '\0';
    }
    return result;
}


Opcode parse_opcode(const char *mnemonic) {
    if (!mnemonic) {
        return OPCODE_INVALID;
    }
    for (size_t i = 0; i < OPCODE_TABLE_SIZE; ++i) {
        if (strcasecmp(mnemonic, OPCODE_TABLE[i].name) == 0) {
            return OPCODE_TABLE[i].opcode;
        }
    }
    return OPCODE_INVALID;
}


const char *opcode_to_string(Opcode opcode) {
    for (size_t i = 0; i < OPCODE_TABLE_SIZE; ++i) {
        if (OPCODE_TABLE[i].opcode == opcode) {
            return OPCODE_TABLE[i].name;
        }
    }
    return "INVALID";
}


void strip_comment(char *line) {
    char *comment_start = strchr(line, '#');
    if (comment_start) {
        *comment_start = '\0';
    }
}


MIPSInstruction parse_assembly_line(const char *line,
                        size_t source_line_number,
                        uint32_t address) {
    MIPSInstruction instruction;
    instruction.source_line_number = source_line_number;
    instruction.address = address;
    instruction.operand_count = 0;

    char *working = strdup(line);
    strip_comment(working);
    working = trim_whitespace(working);
    if (*working == '\0') {
        instruction.opcode = OPCODE_NOOP;
        instruction.operand_count = 0;
        free(working);
        return instruction;
    }

    char *mnemonic = strtok(working, " \t");
    if (!mnemonic) {
        instruction.opcode = OPCODE_INVALID;
        instruction.operand_count = 0;
        free(working);
        return instruction;
    }
    //FUTURE - handle labels
    instruction.opcode = parse_opcode(mnemonic);

    char * operand_str;
    while ((operand_str = strtok(NULL, " \t,"))) {
        if (instruction.operand_count >= MIPS_MAX_OPERANDS) {
            instruction.opcode = OPCODE_INVALID;
            break;
        }
        Operand operand = parse_operand(operand_str);

        instruction.operands[instruction.operand_count++] = operand;
    }

    return instruction;
}


int append_instruction(MIPSProgram *program,
                       const MIPSInstruction *instruction) {
    if (program->instruction_count >= program->instruction_capacity) {
        size_t new_capacity = (program->instruction_capacity == 0) ? 4 : program->instruction_capacity * 2;
        MIPSInstruction *new_instructions = realloc(program->instructions, new_capacity * sizeof(MIPSInstruction));
        if (!new_instructions) {
            fprintf(stderr, "Error: Memory allocation failed while appending instruction.\n");
            return -1;
        }
        program->instructions = new_instructions;
        program->instruction_capacity = new_capacity;
    }
    program->instructions[program->instruction_count++] = *instruction;
    return 0;
}


MIPSProgram load_assembly_file(const char *assembly_path) {
    MIPSProgram program;
    program.instructions = NULL;
    program.instruction_count = 0;
    program.instruction_capacity = 0;
    //FUTURE - handle text_base for multiple VMs
    program.text_base = DEFAULT_TEXT_BASE;
    FILE *file = fopen(assembly_path, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open assembly file '%s'.\n", assembly_path);
        exit(EXIT_FAILURE);
    }
    int line_number = 1;
    char line[MIPS_MAX_SOURCE_LINE];
    while (fgets(line, sizeof(line), file) != NULL) {
        strip_comment(line);
        char *trimmedLine = trim_whitespace(line);
        if (*trimmedLine == '\0') {
            line_number++;
            continue;
        }
        MIPSInstruction instruction = parse_assembly_line(trimmedLine, line_number, 0);
        if (append_instruction(&program, &instruction) != 0) {
            fprintf(stderr, "Error: Failed to append instruction.\n");
            fclose(file);
            exit(EXIT_FAILURE);
        }
        line_number++;
    }
    return program;
}


void destroy_program(MIPSProgram *program) {
    free(program->instructions);
    program->instructions = NULL;
    program->instruction_count = 0;
    program->instruction_capacity = 0;
    program->text_base = 0;
}


VM initialize_vm(unsigned int vm_id,
                  const VMConfig *config) {
    VM temp_vm;
    temp_vm.vm_id = vm_id;
    temp_vm.config = *config;
    temp_vm.program = load_assembly_file(config->vm_binary);
    temp_vm.current_instruction_index = 0;
    temp_vm.halted = false;
    initialize_registers(&temp_vm.registers);
    return temp_vm;
}


void destroy_vm(VM *vm) {
    destroy_program(&vm->program);
    vm->current_instruction_index = 0;
    vm->halted = false;
}


// AI used to speed up the process of writing the code by generating the validate_instruction function from the PDF.
int validate_instruction(const MIPSInstruction *instruction)
{
    if (instruction == NULL)
        return -1;

    switch (instruction->opcode)
    {
        case OPCODE_LI:
            if (instruction->operand_count != 2)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_IMMEDIATE)
                return -1;

            return 0;


        case OPCODE_DUMP_PROCESSOR_STATE:
            if (instruction->operand_count != 0)
                return -1;

            return 0;

        case OPCODE_SNAPSHOT:
            if (instruction->operand_count != 1 ||
                instruction->operands[0].type != OPERAND_LABEL)
                return -1;

            return 0;

        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_ADDU:
        case OPCODE_SUBU:
        case OPCODE_MUL:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER ||
                instruction->operands[2].type != OPERAND_REGISTER)
                return -1;

            return 0;


        case OPCODE_ADDI:
        case OPCODE_ADDIU:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER ||
                instruction->operands[2].type != OPERAND_IMMEDIATE)
                return -1;

            return 0;


        case OPCODE_MULT:
        case OPCODE_DIV:
            if (instruction->operand_count != 2)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER)
                return -1;

            return 0;

        case OPCODE_AND:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER ||
                instruction->operands[2].type != OPERAND_REGISTER)
                return -1;

            return 0;


        case OPCODE_OR:
        case OPCODE_XOR:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER)
                return -1;

            if (instruction->operands[2].type != OPERAND_REGISTER &&
                instruction->operands[2].type != OPERAND_IMMEDIATE)
                return -1;

            return 0;


        case OPCODE_ANDI:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER ||
                instruction->operands[2].type != OPERAND_IMMEDIATE)
                return -1;

            return 0;


        case OPCODE_SLL:
        case OPCODE_SRL:
            if (instruction->operand_count != 3)
                return -1;

            if (instruction->operands[0].type != OPERAND_REGISTER ||
                instruction->operands[1].type != OPERAND_REGISTER ||
                instruction->operands[2].type != OPERAND_IMMEDIATE)
                return -1;


            if (instruction->operands[2].value.immediate < 0 ||
                instruction->operands[2].value.immediate > 31)
                return -1;

            return 0;

        case OPCODE_LW:
        case OPCODE_SW:
        case OPCODE_LUI:
        case OPCODE_LA:
        case OPCODE_MFHI:
        case OPCODE_MFLO:
        case OPCODE_MOVE:
        case OPCODE_BEQ:
        case OPCODE_BNE:
        case OPCODE_BGT:
        case OPCODE_BGE:
        case OPCODE_BLT:
        case OPCODE_BLE:
        case OPCODE_SLT:
        case OPCODE_SLTI:
        case OPCODE_J:
        case OPCODE_JR:
        case OPCODE_JAL:
        case OPCODE_SYSCALL:
            return -1;


        case OPCODE_INVALID:
        default:
            return -1;
    }
}



int execute_add(VM *vm, const MIPSInstruction *instruction) {
    int32_t src1 = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);
    int32_t src2 = (int32_t)read_register(&vm->registers, instruction->operands[2].value.register_number);
    int32_t result = src1 + src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_sub(VM *vm, const MIPSInstruction *instruction) {
    int32_t src1 = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);
    int32_t src2 = (int32_t)read_register(&vm->registers, instruction->operands[2].value.register_number);
    int32_t result = src1 - src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_addi(VM *vm, const MIPSInstruction *instruction) {
    int32_t src = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);
    int32_t immediate = instruction->operands[2].value.immediate;
    int32_t result = src + immediate;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_addu(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src1 = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t src2 = read_register(&vm->registers, instruction->operands[2].value.register_number);
    uint32_t result = src1 + src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_subu(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src1 = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t src2 = read_register(&vm->registers, instruction->operands[2].value.register_number);
    uint32_t result = src1 - src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_addiu(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src = read_register(&vm->registers, instruction->operands[1].value.register_number);
    int32_t immediate = instruction->operands[2].value.immediate;
    uint32_t result = src + immediate;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_mul(VM *vm, const MIPSInstruction *instruction) {
    int32_t src1 = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);
    int32_t src2 = (int32_t)read_register(&vm->registers, instruction->operands[2].value.register_number);
    int64_t result = (int64_t)src1 * (int64_t)src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, (uint32_t)(result & 0xFFFFFFFF));
    return 0;
}

int execute_mult(VM *vm, const MIPSInstruction *instruction) {
    int32_t src1 = (int32_t)read_register(&vm->registers, instruction->operands[0].value.register_number);
    int32_t src2 = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);
    int64_t result = (int64_t)src1 * (int64_t)src2;
    vm->registers.hi = (uint32_t)((result >> 32) & 0xFFFFFFFF);
    vm->registers.lo = (uint32_t)(result & 0xFFFFFFFF);
    return 0;
}

int execute_div(VM *vm, const MIPSInstruction *instruction) {
    int32_t src1 = (int32_t)read_register(&vm->registers, instruction->operands[0].value.register_number);
    int32_t src2 = (int32_t)read_register(&vm->registers, instruction->operands[1].value.register_number);

    if (src2 == 0) {
        fprintf(stderr, "Error: Division by zero in div instruction.\n");
        return -1;
    }

    vm->registers.lo = (uint32_t)(src1 / src2);
    vm->registers.hi = (uint32_t)(src1 % src2);
    return 0;
}

int execute_and(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src1 = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t src2 = read_register(&vm->registers, instruction->operands[2].value.register_number);
    uint32_t result = src1 & src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_or(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src1 = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t src2;

    if (instruction->operands[2].type == OPERAND_REGISTER) {
        src2 = read_register(&vm->registers, instruction->operands[2].value.register_number);
    } else if (instruction->operands[2].type == OPERAND_IMMEDIATE) {
        src2 = (uint32_t)instruction->operands[2].value.immediate;
    } else {
        fprintf(stderr, "Error: Invalid operand type for OR operation.\n");
        return -1;
    }

    uint32_t result = src1 | src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_andi(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t immediate = (uint32_t)instruction->operands[2].value.immediate;
    uint32_t result = src & immediate;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_xor(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src1 = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t src2;

    if (instruction->operands[2].type == OPERAND_REGISTER) {
        src2 = read_register(&vm->registers, instruction->operands[2].value.register_number);
    } else if (instruction->operands[2].type == OPERAND_IMMEDIATE) {
        src2 = (uint32_t)instruction->operands[2].value.immediate;
    } else {
        fprintf(stderr, "Error: Invalid operand type for XOR operation.\n");
        return -1;
    }

    uint32_t result = src1 ^ src2;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_sll(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t shift_amount = (uint32_t)instruction->operands[2].value.immediate;

    if (shift_amount > 31) {
        fprintf(stderr, "Error: Shift amount must be between 0 and 31.\n");
        return -1;
    }

    uint32_t result = src << shift_amount;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}

int execute_srl(VM *vm, const MIPSInstruction *instruction) {
    uint32_t src = read_register(&vm->registers, instruction->operands[1].value.register_number);
    uint32_t shift_amount = (uint32_t)instruction->operands[2].value.immediate;

    if (shift_amount > 31) {
        fprintf(stderr, "Error: Shift amount must be between 0 and 31.\n");
        return -1;
    }

    uint32_t result = src >> shift_amount;
    write_register(&vm->registers, instruction->operands[0].value.register_number, result);
    return 0;
}


void execute_li(VM *vm, const MIPSInstruction *instruction) {
    if (instruction->operand_count != 2) {
        fprintf(stderr, "Error: li instruction requires 2 operands.\n");
        return;
    }
    unsigned int dest_reg = instruction->operands[0].value.register_number;
    int32_t immediate_value = instruction->operands[1].value.immediate;

    write_register(&vm->registers, dest_reg, (uint32_t)immediate_value);
}


void dump_processor_state(const MIPSRegisters *registers) {
    for (unsigned int i = 0; i < MIPS_REGISTER_COUNT; ++i) {
        printf("R%u=%d\n", i, (int32_t)registers->gpr[i]);
    }
}


static int write_data(FILE *file, const void *data, size_t size) {
    return fwrite(data, 1, size, file) == size ? 0 : -1;
}

static int write_string(FILE *file, const char *value) {
    uint64_t length = value ? (uint64_t)strlen(value) : 0;

    if (write_data(file, &length, sizeof(length)) != 0) {
        return -1;
    }
    if (length > 0 && write_data(file, value, (size_t)length) != 0) {
        return -1;
    }
    return 0;
}

static int write_operand(FILE *file, const Operand *operand) {
    uint32_t type = (uint32_t)operand->type;

    if (write_data(file, &type, sizeof(type)) != 0) {
        return -1;
    }

    switch (operand->type) {
        case OPERAND_REGISTER: {
            uint32_t reg_num = operand->value.register_number;
            return write_data(file, &reg_num, sizeof(reg_num));
        }
        case OPERAND_IMMEDIATE:
            return write_data(file, &operand->value.immediate, sizeof(operand->value.immediate));
        case OPERAND_LABEL:
            return write_string(file, operand->value.label);
        case OPERAND_NONE:
            return 0;
        default:
            return -1;
    }
}


void save_snapshot(const VM *vm, const char *snapshot_path) {
    if (!vm || !snapshot_path || snapshot_path[0] == '\0') {
        fprintf(stderr, "Error: Invalid VM or snapshot path.\n");
        return;
    }

    FILE *file = fopen(snapshot_path, "wb");
    if (!file) {
        fprintf(stderr, "Error: Could not open snapshot file '%s' for writing.\n", snapshot_path);
        return;
    }

    static const unsigned char magic[8] = { 'H', 'V', 'S', 'N', 'A', 'P', '0', '1' };
    const uint32_t format_version = 1;

    uint64_t execution_slice = (uint64_t)vm->config.vm_exec_slice_in_instructions;
    uint64_t instruction_count = (uint64_t)vm->program.instruction_count;
    uint64_t current_instruction_index = (uint64_t)vm->current_instruction_index;
    uint32_t halted = vm->halted ? 1u : 0u;
    uint32_t text_base = vm->program.text_base;
    uint32_t vm_id = vm->vm_id;

    if (write_data(file, magic, sizeof(magic)) != 0 ||
        write_data(file, &format_version, sizeof(format_version)) != 0 ||
        write_data(file, &vm_id, sizeof(vm_id)) != 0 ||
        write_data(file, &execution_slice, sizeof(execution_slice)) != 0 ||
        write_string(file, vm->config.vm_binary) != 0 ||
        write_data(file, vm->registers.gpr, sizeof(vm->registers.gpr)) != 0 ||
        write_data(file, &vm->registers.hi, sizeof(vm->registers.hi)) != 0 ||
        write_data(file, &vm->registers.lo, sizeof(vm->registers.lo)) != 0 ||
        write_data(file, &vm->registers.pc, sizeof(vm->registers.pc)) != 0 ||
        write_data(file, &current_instruction_index, sizeof(current_instruction_index)) != 0 ||
        write_data(file, &halted, sizeof(halted)) != 0 ||
        write_data(file, &text_base, sizeof(text_base)) != 0 ||
        write_data(file, &instruction_count, sizeof(instruction_count)) != 0) {
        goto write_error;
    }

    for (size_t i = 0; i < vm->program.instruction_count; ++i) {
        const MIPSInstruction *instr = &vm->program.instructions[i];
        if (instr->operand_count > MIPS_MAX_OPERANDS) {
            goto write_error;
        }

        uint32_t opcode = (uint32_t)instr->opcode;
        uint32_t operand_count = (uint32_t)instr->operand_count;
        uint64_t source_line = (uint64_t)instr->source_line_number;

        if (write_data(file, &opcode, sizeof(opcode)) != 0 ||
            write_data(file, &operand_count, sizeof(operand_count)) != 0 ||
            write_data(file, &instr->address, sizeof(instr->address)) != 0 ||
            write_data(file, &source_line, sizeof(source_line)) != 0) {
            goto write_error;
        }

        for (size_t j = 0; j < MIPS_MAX_OPERANDS; ++j) {
            if (write_operand(file, &instr->operands[j]) != 0) {
                goto write_error;
            }
        }
    }

    fclose(file);
    return;

write_error:
    fclose(file);
    fprintf(stderr, "Error: Failed while writing snapshot file '%s'.\n", snapshot_path);
}

static int execute_snapshot(VM *vm, const MIPSInstruction *instruction) {
    if (!vm || !instruction || instruction->operand_count != 1 ||
        instruction->operands[0].type != OPERAND_LABEL) {
        return -1;
    }

    VM snapshot_vm = *vm;
    snapshot_vm.current_instruction_index++;
    snapshot_vm.registers.pc += MIPS_WORD_SIZE;

    save_snapshot(&snapshot_vm, instruction->operands[0].value.label);
    return 0;
}


static int read_data(FILE *file, void *data, size_t size) {
    return fread(data, 1, size, file) == size ? 0 : -1;
}

static int read_string(FILE *file, char *destination, size_t capacity) {
    uint64_t length;

    if (read_data(file, &length, sizeof(length)) != 0 || length >= capacity) {
        return -1;
    }
    if (length > 0 && read_data(file, destination, (size_t)length) != 0) {
        return -1;
    }
    destination[length] = '\0';
    return 0;
}

static int read_operand(FILE *file, Operand *operand) {
    uint32_t type;

    if (read_data(file, &type, sizeof(type)) != 0 || type > OPERAND_LABEL) {
        return -1;
    }
    operand->type = (OperandType)type;

    switch (operand->type) {
        case OPERAND_NONE:
            return 0;
        case OPERAND_REGISTER: {
            uint32_t reg_num;
            if (read_data(file, &reg_num, sizeof(reg_num)) != 0 ||
                reg_num >= MIPS_REGISTER_COUNT) {
                return -1;
            }
            operand->value.register_number = reg_num;
            return 0;
        }
        case OPERAND_IMMEDIATE:
            return read_data(file, &operand->value.immediate, sizeof(operand->value.immediate));
        case OPERAND_LABEL:
            return read_string(file, operand->value.label, sizeof(operand->value.label));
        default:
            return -1;
    }
}


VM load_snapshot(const char *snapshot_path) {
    if (!snapshot_path || snapshot_path[0] == '\0') {
        fprintf(stderr, "Error: Invalid snapshot path.\n");
        exit(EXIT_FAILURE);
    }

    FILE *file = fopen(snapshot_path, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open snapshot file '%s' for reading.\n", snapshot_path);
        exit(EXIT_FAILURE);
    }

    VM vm;
    memset(&vm, 0, sizeof(vm));

    static const unsigned char expected_magic[8] = { 'H', 'V', 'S', 'N', 'A', 'P', '0', '1' };
    unsigned char magic[8];
    uint32_t format_version;
    uint32_t vm_id;
    uint64_t execution_slice;
    uint64_t instruction_count;
    uint64_t current_instruction_index;
    uint32_t halted;
    uint32_t text_base;

    if (read_data(file, magic, sizeof(magic)) != 0 ||
        memcmp(magic, expected_magic, sizeof(expected_magic)) != 0 ||
        read_data(file, &format_version, sizeof(format_version)) != 0 ||
        format_version != 1) {
        goto failure;
    }

    if (read_data(file, &vm_id, sizeof(vm_id)) != 0 ||
        read_data(file, &execution_slice, sizeof(execution_slice)) != 0 ||
        execution_slice == 0 || execution_slice > SIZE_MAX ||
        read_string(file, vm.config.vm_binary, sizeof(vm.config.vm_binary)) != 0 ||
        read_data(file, vm.registers.gpr, sizeof(vm.registers.gpr)) != 0 ||
        read_data(file, &vm.registers.hi, sizeof(vm.registers.hi)) != 0 ||
        read_data(file, &vm.registers.lo, sizeof(vm.registers.lo)) != 0 ||
        read_data(file, &vm.registers.pc, sizeof(vm.registers.pc)) != 0 ||
        read_data(file, &current_instruction_index, sizeof(current_instruction_index)) != 0 ||
        read_data(file, &halted, sizeof(halted)) != 0 || halted > 1 ||
        read_data(file, &text_base, sizeof(text_base)) != 0 ||
        read_data(file, &instruction_count, sizeof(instruction_count)) != 0) {
        goto failure;
    }

    if (instruction_count > SIZE_MAX ||
        instruction_count > SIZE_MAX / sizeof(MIPSInstruction) ||
        current_instruction_index > instruction_count) {
        goto failure;
    }

    vm.vm_id = vm_id;
    vm.config.vm_exec_slice_in_instructions = (size_t)execution_slice;
    vm.program.text_base = text_base;
    vm.program.instruction_count = (size_t)instruction_count;
    vm.program.instruction_capacity = (size_t)instruction_count;
    vm.current_instruction_index = (size_t)current_instruction_index;
    vm.halted = (halted != 0);

    if (instruction_count > 0) {
        vm.program.instructions = calloc((size_t)instruction_count, sizeof(MIPSInstruction));
        if (!vm.program.instructions) {
            goto failure;
        }
    }

    for (size_t i = 0; i < vm.program.instruction_count; ++i) {
        MIPSInstruction *instr = &vm.program.instructions[i];
        uint32_t opcode, operand_count;
        uint64_t source_line;

        if (read_data(file, &opcode, sizeof(opcode)) != 0 || opcode > OPCODE_SYSCALL ||
            read_data(file, &operand_count, sizeof(operand_count)) != 0 || operand_count > MIPS_MAX_OPERANDS ||
            read_data(file, &instr->address, sizeof(instr->address)) != 0 ||
            read_data(file, &source_line, sizeof(source_line)) != 0 ||
            source_line > SIZE_MAX) {
            goto failure;
        }

        instr->opcode = (Opcode)opcode;
        instr->operand_count = (size_t)operand_count;
        instr->source_line_number = (size_t)source_line;

        for (size_t j = 0; j < MIPS_MAX_OPERANDS; ++j) {
            if (read_operand(file, &instr->operands[j]) != 0) {
                goto failure;
            }
        }
    }

    fclose(file);
    return vm;

failure:
    fclose(file);
    free(vm.program.instructions);
    fprintf(stderr, "Error: Invalid or truncated snapshot file '%s'.\n", snapshot_path);
    exit(EXIT_FAILURE);
}


int execute_arithmetic_instruction(VM *vm,
                                   const MIPSInstruction *instruction) {
    switch (instruction->opcode) {
        case OPCODE_ADD:
            return execute_add(vm, instruction);
        case OPCODE_SUB:
            return execute_sub(vm, instruction);
        case OPCODE_ADDI:
            return execute_addi(vm, instruction);
        case OPCODE_ADDU:
            return execute_addu(vm, instruction);
        case OPCODE_SUBU:
            return execute_subu(vm, instruction);
        case OPCODE_ADDIU:
            return execute_addiu(vm, instruction);
        case OPCODE_MUL:
            return execute_mul(vm, instruction);
        case OPCODE_MULT:
            return execute_mult(vm, instruction);
        case OPCODE_DIV:
            return execute_div(vm, instruction);
        default:
            fprintf(stderr, "Error: Unsupported arithmetic instruction %s\n", opcode_to_string(instruction->opcode));
            return -1;
    }
}


int execute_logical_instruction(VM *vm,
                                const MIPSInstruction *instruction) {
    switch (instruction->opcode) {
        case OPCODE_AND:
            return execute_and(vm, instruction);
        case OPCODE_OR:
            return execute_or(vm, instruction);
        case OPCODE_ANDI:
            return execute_andi(vm, instruction);
        case OPCODE_XOR:
            return execute_xor(vm, instruction);
        case OPCODE_SLL:
            return execute_sll(vm, instruction);
        case OPCODE_SRL:
            return execute_srl(vm, instruction);
        default:
            fprintf(stderr, "Error: Unsupported logical instruction %s\n", opcode_to_string(instruction->opcode));
            return -1;
    }
}


int execute_pseudo_instruction(VM *vm,
                               const MIPSInstruction *instruction) {
    switch (instruction->opcode) {
        case OPCODE_LI:
            execute_li(vm, instruction);
            return 0;

        case OPCODE_DUMP_PROCESSOR_STATE:
            dump_processor_state(&vm->registers);
            return 0;

        case OPCODE_SNAPSHOT:
            return execute_snapshot(vm, instruction);

        default:
            fprintf(stderr, "Error: Unsupported pseudo-instruction %s\n", opcode_to_string(instruction->opcode));
            return -1;
    }
}

int execute_instruction(VM *vm,
                        const MIPSInstruction *instruction) {
    switch (instruction->opcode) {
        case OPCODE_LI:
        case OPCODE_DUMP_PROCESSOR_STATE:
        case OPCODE_SNAPSHOT:
            return execute_pseudo_instruction(vm, instruction);

        case OPCODE_ADD:
        case OPCODE_SUB:
        case OPCODE_ADDI:
        case OPCODE_ADDU:
        case OPCODE_SUBU:
        case OPCODE_ADDIU:
        case OPCODE_MUL:
        case OPCODE_MULT:
        case OPCODE_DIV:
            return execute_arithmetic_instruction(vm, instruction);

        case OPCODE_AND:
        case OPCODE_OR:
        case OPCODE_ANDI:
        case OPCODE_XOR:
        case OPCODE_SLL:
        case OPCODE_SRL:
            return execute_logical_instruction(vm, instruction);

        default:
            fprintf(stderr, "Error: Unsupported opcode %s\n", opcode_to_string(instruction->opcode));
            return -1;
    }
}


// FUTURE - location for control flow
void advance_program_counter(VM *vm) {
    vm->registers.pc += MIPS_WORD_SIZE;
    vm->current_instruction_index++;
}


bool vm_has_next_instruction(const VM *vm) {
    return !vm->halted && vm->current_instruction_index < vm->program.instruction_count;
}


void report_parse_error(const char *file_path,
                        size_t line_number,
                        const char *line,
                        const char *message) {
    fprintf(stderr, "Parse error in %s at line %zu: %s\n%s\n",
            file_path, line_number, message, line);
}


void report_execution_error(const VM *vm,
                            const MIPSInstruction *instruction,
                            const char *message) {
    fprintf(stderr, "Execution error in VM %u at PC 0x%08X, instruction %s "
                    "(source line %zu): %s\n",
            vm->vm_id,
            vm->registers.pc,
            opcode_to_string(instruction->opcode),
            instruction->source_line_number,
            message);
}


size_t execute_vm_slice(VM *vm) {
    size_t executed_count = 0;
    while (executed_count < vm->config.vm_exec_slice_in_instructions && vm_has_next_instruction(vm)) {
        MIPSInstruction *instruction = &vm->program.instructions[vm->current_instruction_index];
        if (validate_instruction(instruction) != 0) {
            report_execution_error(vm, instruction, "Invalid instruction");
            vm->halted = true;
            break;
        }
        if (execute_instruction(vm, instruction) != 0) {
            report_execution_error(vm, instruction, "Execution error");
            vm->halted = true;
            break;
        }
        advance_program_counter(vm);
        executed_count++;
    }
    return executed_count;
}


Hypervisor initialize_hypervisor() {
    Hypervisor hypervisor;
    hypervisor.vm_count = 0;
    hypervisor.vm_capacity = MIPS_MAX_VMS;
    hypervisor.current_vm_index = 0;
    return hypervisor;
}


int add_vm(Hypervisor *hypervisor,
           unsigned int vm_id,
           const VMConfig *config) {
    VM vm = initialize_vm(vm_id, config);

    hypervisor->vms[hypervisor->vm_count++] = vm;
    return 0;
}


//FUTURE
// void context_switch(Hypervisor *hypervisor,
//                     size_t next_vm_index);


bool all_vms_halted(const Hypervisor *hypervisor) {
    for (size_t i = 0; i < hypervisor->vm_count; ++i) {
        if (!hypervisor->vms[i].halted && hypervisor->vms[i].current_instruction_index < hypervisor->vms[i].program.instruction_count) {
            return false;
        }
    }
    return true;
}


void run_hypervisor(Hypervisor *hypervisor) {
    while (!all_vms_halted(hypervisor)) {
        VM *current_vm = &hypervisor->vms[hypervisor->current_vm_index];
        if (!current_vm->halted) {
            execute_vm_slice(current_vm);
        }
        hypervisor->current_vm_index = (hypervisor->current_vm_index + 1) % hypervisor->vm_count;
    }
}


void destroy_hypervisor(Hypervisor *hypervisor) {
    for (size_t i = 0; i < hypervisor->vm_count; ++i) {
        destroy_vm(&hypervisor->vms[i]);
    }
    hypervisor->vm_count = 0;
    hypervisor->current_vm_index = 0;
}


int load_vms_from_configs(Hypervisor *hypervisor,
                          char **config_paths,
                          const bool *is_snapshot,
                          size_t config_count) {
    for (size_t i = 0; i < config_count; ++i) {
        if (!config_paths[i]) {
            continue;
        }
        if (is_snapshot[i]) {
            VM vm = load_snapshot(config_paths[i]);
            vm.vm_id = hypervisor->vm_count;
            hypervisor->vms[hypervisor->vm_count++] = vm;
        } else {
            VMConfig vmconfig = { 0 };
            if (parse_config_file(config_paths[i], &vmconfig) != 0 ||
                vmconfig.vm_exec_slice_in_instructions == 0 ||
                vmconfig.vm_binary[0] == '\0') {
                return -1;
            }
            add_vm(hypervisor, hypervisor->vm_count, &vmconfig);
        }
    }
    return 0;
}


int main(int argc, char **argv) {
    ConfigPaths config = parse_command_line(argc, argv);
    if (!config.ok) {
        return EXIT_FAILURE;
    }
    if (config.count == 0) {
        fprintf(stderr, "Error: No VM configuration or snapshot files specified.\n");
        return EXIT_FAILURE;
    }

    Hypervisor hypervisor = initialize_hypervisor();
    if (load_vms_from_configs(&hypervisor, config.paths,
                              config.is_snapshot, config.count) != 0) {
        destroy_hypervisor(&hypervisor);
        return EXIT_FAILURE;
    }
    run_hypervisor(&hypervisor);
    destroy_hypervisor(&hypervisor);
    return EXIT_SUCCESS;
}
