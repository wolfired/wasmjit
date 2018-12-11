/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
  Copyright (c) 2018 Rian Hunter et. al, see AUTHORS file.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 */

#include <wasmjit/compile.h>
#include <wasmjit/runtime.h>
#include <wasmjit/static_runtime.h>
#include <wasmjit/util.h>
#include <wasmjit/vector.h>

#include <stdio.h>

#if defined(__linux__) || defined(__OpenBSD__)

#include <elf.h>

#if defined(__OpenBSD__)

#include <amd64/reloc.h>

#endif

struct Relocations {
    size_t n_elts;
    Elf64_Rela *elts;
};

static DEFINE_VECTOR_GROW(relocations, struct Relocations);

int add_relocation(struct Relocations *relocations, Elf64_Addr offset, Elf64_Xword type, Elf64_Xword sym, Elf64_Sxword addend) {
    Elf64_Rela *newelt;
    if (!relocations_grow(relocations, 1))
        return 0;
    newelt = &relocations->elts[relocations->n_elts - 1];
    newelt->r_offset = offset;
    newelt->r_info = ELF64_R_INFO(sym, type);
    newelt->r_addend = addend;
    return 1;
}

struct Symbols {
    size_t n_elts;
    Elf64_Sym *elts;
};

static DEFINE_VECTOR_GROW(symbols, struct Symbols);

int add_symbol(struct Symbols *symbols, uint32_t name, unsigned char type, unsigned char bind, unsigned char other, uint16_t shndx, Elf64_Addr value, uint64_t size) {
    Elf64_Sym *newelt;
    if (!symbols_grow(symbols, 1))
        return 0;
    newelt = &symbols->elts[symbols->n_elts - 1];
    newelt->st_name = name;
    newelt->st_info = ELF64_ST_INFO(bind, type);
    newelt->st_other = other;
    newelt->st_shndx = shndx;
    newelt->st_value = value;
    newelt->st_size = size;
    return 1;
}

static void read_constant_expression(struct Value *value, size_t n_instructions, struct Instr *instructions) {
    (void)n_instructions;
    assert(n_instructions == 1);

    switch (instructions[0].opcode) {
    case OPCODE_I32_CONST:
        value->type = VALTYPE_I32;
        value->data.i32 = instructions[0].data.i32_const.value;
        break;
    case OPCODE_I64_CONST:
        value->type = VALTYPE_I64;
        value->data.i64 = instructions[0].data.i64_const.value;
        break;
    case OPCODE_F32_CONST:
        value->type = VALTYPE_F32;
        value->data.f32 = instructions[0].data.f32_const.value;
        break;
    case OPCODE_F64_CONST:
        value->type = VALTYPE_F64;
        value->data.f64 = instructions[0].data.f64_const.value;
        break;
    default:
        assert(0);
        break;
    }
}

void *wasmjit_output_elf_relocatable(const char *module_name, const struct Module *module, size_t *outsize) {
    enum {
        NULL_SECTION_IDX,
        TEXT_SECTION_IDX,
        RELA_TEXT_SECTION_IDX,
        DATA_SECTION_IDX,
        RELA_DATA_SECTION_IDX,
        BSS_SECTION_IDX,
        INIT_ARRAY_SECTION_IDX,
        RELA_INIT_ARRAY_SECTION_IDX,
        SYMTAB_SECTION_IDX,
        STRTAB_SECTION_IDX,
        SHSTRTAB_SECTION_IDX,
        NSECTIONS,
    };
    char buf[0x100];
    struct SizedBuffer outputv = {0, NULL};
    struct SizedBuffer *output = &outputv;
    struct SizedBuffer strtabv = {0, NULL};
    struct SizedBuffer *strtab = &strtabv;
    struct Relocations data_relocations_v = {0, NULL};
    struct Relocations *data_relocations = &data_relocations_v;
    struct Relocations text_relocations_v = {0, NULL};
    struct Relocations *text_relocations = &text_relocations_v;
    struct Symbols symbolsv = {0, NULL};
    struct Symbols *symbols = &symbolsv;
    struct {
        size_t n_elts;
        struct {
            size_t import_idx;
            size_t symidx;
            size_t offset;
            struct FuncType type;
        } * elts;
    } module_funcs = {0, NULL};
    struct {
        size_t n_elts;
        struct {
            size_t import_idx;
            size_t symidx;
            size_t offset;
            struct TableType type;
        } * elts;
    } module_tables = {0, NULL};
    struct {
        size_t n_elts;
        struct {
            size_t import_idx;
            size_t symidx;
            size_t offset;
            struct MemoryType type;
        } * elts;
    } module_mems = {0, NULL};
    struct {
        size_t n_elts;
        struct {
            size_t import_idx;
            size_t symidx;
            size_t offset;
            struct GlobalType type;
            size_t init_symidx;
        } * elts;
    } module_globals = {0, NULL};
    Elf64_Ehdr hdr = {
        {
            ELFMAG0,
            ELFMAG1,
            ELFMAG2,
            ELFMAG3,
            ELFCLASS64,
            ELFDATA2LSB,
            EV_CURRENT,
            ELFOSABI_SYSV,
        },
        ET_REL,
        EM_X86_64,
        EV_CURRENT,
        0,
        0,
        sizeof(Elf64_Ehdr),
        0,
        0,
        0,
        0,
        sizeof(Elf64_Shdr),
        NSECTIONS,
        SHSTRTAB_SECTION_IDX,
    };
    Elf64_Shdr zero_sec, text_sec, rela_text_sec, data_sec, rela_data_sec, bss_sec, init_array_sec, rela_init_array_sec, symtab_sec, strtab_sec, shstrtab_sec;
    size_t i, section_header_start, data_section_start, text_section_start, text_section_end, rela_text_section_start, rela_data_section_start, bss_section_start, symtab_section_start, init_array_section_start, rela_init_array_section_start,
        strtab_section_start, shstrtab_section_start, type_inst_start, func_inst_start, table_inst_start, mem_inst_start, global_inst_start, array_symbol_start, constructor_symbol, global_inits_start, data_symbol_start, data_struct_symbol_start,
        element_symbol_start, element_struct_symbol_start, module_symbol, static_module_symbol, global_symbol_start, init_static_module_symbol, resolve_indirect_call_symbol, trap_symbol, func_code_start, n_imported_funcs, n_imported_tables,
        n_imported_mems, n_imported_globals, funcs_offset, tables_offset, mems_offset, globals_offset, types_symbol_start;
    size_t bss_size = 0;
    struct ModuleTypes module_types;
    struct _ObjectIter {
        size_t *inst_start;
        size_t nelts;
        unsigned type;
        const char *inst_type;
    } itr[] = {
        {&func_inst_start, module->function_section.n_typeidxs, IMPORT_DESC_TYPE_FUNC, "function"},
        {&table_inst_start, module->table_section.n_tables, IMPORT_DESC_TYPE_TABLE, "table"},
        {&mem_inst_start, module->memory_section.n_memories, IMPORT_DESC_TYPE_MEM, "memory"},
        {&global_inst_start, module->global_section.n_globals, IMPORT_DESC_TYPE_GLOBAL, "global"},
    };
    struct {
        size_t n_elts;
        struct MemoryReferences *elts;
    } code_memrefs = {0, NULL};

    memset(&module_types, 0, sizeof(module_types));

#define OUT(buf, size)                                                                                                                                                                                                                                         \
    do {                                                                                                                                                                                                                                                       \
        if (!output_buf(output, buf, size))                                                                                                                                                                                                                    \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

#define OUTE(buf, nelts, elt_size)                                                                                                                                                                                                                             \
    do {                                                                                                                                                                                                                                                       \
        size_t tsize;                                                                                                                                                                                                                                          \
                                                                                                                                                                                                                                                               \
        if (__builtin_umull_overflow(nelts, elt_size, &tsize))                                                                                                                                                                                                 \
            goto error;                                                                                                                                                                                                                                        \
        OUT(buf, tsize);                                                                                                                                                                                                                                       \
    } while (0)

    /* header */
    OUT(&hdr, sizeof(hdr));
    memset(&zero_sec, 0, sizeof(zero_sec));

    section_header_start = output->n_elts;
    OUT(&zero_sec, sizeof(zero_sec));
    /* section headers */
    OUT(&text_sec, sizeof(text_sec));
    OUT(&rela_text_sec, sizeof(rela_text_sec));
    OUT(&data_sec, sizeof(data_sec));
    OUT(&rela_data_sec, sizeof(rela_data_sec));
    OUT(&bss_sec, sizeof(bss_sec));
    OUT(&init_array_sec, sizeof(init_array_sec));
    OUT(&rela_init_array_sec, sizeof(rela_init_array_sec));
    OUT(&symtab_sec, sizeof(symtab_sec));
    OUT(&strtab_sec, sizeof(strtab_sec));
    OUT(&shstrtab_sec, sizeof(shstrtab_sec));

#define ADD_DATA_SYMBOL_OFF_STR(_str, _off, size_)                                                                                                                                                                                                             \
    do {                                                                                                                                                                                                                                                       \
        if (!add_symbol(symbols, (_str), STT_OBJECT, STB_LOCAL, STV_DEFAULT, DATA_SECTION_IDX, (_off) + output->n_elts - data_section_start, (size_)))                                                                                                         \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

#define ADD_DATA_SYMBOL_STR(_str, _size) ADD_DATA_SYMBOL_OFF_STR((_str), 0, (_size))
#define ADD_DATA_SYMBOL_OFF(_off, _size) ADD_DATA_SYMBOL_OFF_STR(0, (_off), (_size))

#define ADD_DATA_SYMBOL(_size) ADD_DATA_SYMBOL_OFF_STR(0, 0, (_size))

#define ADD_FUNC_SYMBOL_STR(_str, _size)                                                                                                                                                                                                                       \
    do {                                                                                                                                                                                                                                                       \
        if (!add_symbol(symbols, (_str), STT_FUNC, STB_LOCAL, STV_DEFAULT, TEXT_SECTION_IDX, output->n_elts - text_section_start, (_size)))                                                                                                                    \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

#define ADD_DATA_PTR_RELOCATION_RAW(_raw_offset, _rsymidx)                                                                                                                                                                                                     \
    do {                                                                                                                                                                                                                                                       \
        if (!add_relocation(data_relocations, (_raw_offset)-data_section_start, R_X86_64_64, (_rsymidx), 0))                                                                                                                                                   \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

#define ADD_DATA_PTR_RELOCATION(_offset, _rsymidx)                                                                                                                                                                                                             \
    do {                                                                                                                                                                                                                                                       \
        ADD_DATA_PTR_RELOCATION_RAW((_offset) + output->n_elts, *(_rsymidx));                                                                                                                                                                                  \
        (*(_rsymidx))++;                                                                                                                                                                                                                                       \
    } while (0)

#define ADD_FUNC_PTR_RELOCATION_RAW(_raw_offset, _rsymidx, _addend)                                                                                                                                                                                            \
    do {                                                                                                                                                                                                                                                       \
        if (!add_relocation(text_relocations, (_raw_offset)-text_section_start, R_X86_64_64, (_rsymidx), (_addend)))                                                                                                                                           \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

#define LVECTOR_GROW(sstack, n_elts)                                                                                                                                                                                                                           \
    do {                                                                                                                                                                                                                                                       \
        if (!VECTOR_GROW((sstack), (n_elts)))                                                                                                                                                                                                                  \
            goto error;                                                                                                                                                                                                                                        \
    } while (0)

    /* add null symbol */
    if (!add_symbol(symbols, 0, 0, 0, 0, 0, 0, 0))
        goto error;

    buf[0] = '\0';
    if (!output_buf(strtab, buf, 1))
        goto error;

    /* add imported symbol types */
    for (i = 0; i < module->import_section.n_imports; ++i) {
        struct ImportSectionImport *import = &module->import_section.imports[i];

        switch (import->desc_type) {
        case IMPORT_DESC_TYPE_FUNC: {
            size_t idx = module_funcs.n_elts;
            LVECTOR_GROW(&module_funcs, 1);
            module_funcs.elts[idx].import_idx = i;
            module_funcs.elts[idx].type = module->type_section.types[import->desc.functypeidx];
            break;
        }
        case IMPORT_DESC_TYPE_TABLE: {
            size_t idx = module_tables.n_elts;
            LVECTOR_GROW(&module_tables, 1);
            module_tables.elts[idx].import_idx = i;
            module_tables.elts[idx].type = import->desc.tabletype;
            break;
        }
        case IMPORT_DESC_TYPE_MEM: {
            size_t idx = module_mems.n_elts;
            LVECTOR_GROW(&module_mems, 1);
            module_mems.elts[idx].import_idx = i;
            module_mems.elts[idx].type = import->desc.memtype;
            break;
        }
        case IMPORT_DESC_TYPE_GLOBAL: {
            size_t idx = module_globals.n_elts;
            LVECTOR_GROW(&module_globals, 1);
            module_globals.elts[idx].import_idx = i;
            module_globals.elts[idx].type = import->desc.globaltype;
            break;
        }
        default:
            assert(0);
            break;
        }
    }

    n_imported_funcs = module_funcs.n_elts;
    n_imported_tables = module_tables.n_elts;
    n_imported_mems = module_mems.n_elts;
    n_imported_globals = module_globals.n_elts;

    /* align to 8 bytes */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    /* add data */
    data_section_start = output->n_elts;

    /* add types */
    {
        type_inst_start = symbols->n_elts;
        for (i = 0; i < module->type_section.n_types; ++i) {
            struct FuncType *ft = &module->type_section.types[i];
            ADD_DATA_SYMBOL(sizeof(*ft));
            OUT(ft, sizeof(*ft));
        }
    }

    for (i = 0; i < sizeof(itr) / sizeof(itr[0]); ++i) {
        struct _ObjectIter *pitr = &itr[i];
        size_t j;

        /* object for every table, memory, global */
        *(pitr->inst_start) = output->n_elts;
        for (j = 0; j < pitr->nelts; ++j) {
            size_t ret, string_offset;

            string_offset = strtab->n_elts;
            ret = snprintf(buf, sizeof(buf), "%s_%zu", pitr->inst_type, j);
            if (!output_buf(strtab, buf, ret + 1))
                goto error;

            switch (pitr->type) {
            case IMPORT_DESC_TYPE_FUNC: {
                struct FuncInst funcinstv;
                struct FuncInst *funcinst = &funcinstv;
                uint32_t tidx = module->function_section.typeidxs[j];

                memset(funcinst, 0, sizeof(*funcinst));

                funcinst->type = module->type_section.types[tidx];

                /* Fill module pointer later */
                /* Fill compile_code pointer later */

                size_t idx = module_funcs.n_elts;
                LVECTOR_GROW(&module_funcs, 1);
                module_funcs.elts[idx].offset = output->n_elts;
                module_funcs.elts[idx].symidx = symbols->n_elts;
                module_funcs.elts[idx].type = module->type_section.types[tidx];

                ADD_DATA_SYMBOL_STR(string_offset, sizeof(*funcinst));
                OUT(funcinst, sizeof(*funcinst));

                break;
            }
            case IMPORT_DESC_TYPE_TABLE: {
                struct TableInst tableinstv;
                struct TableInst *tableinst = &tableinstv;
                struct TableType *tt = &module->table_section.tables[j];

                memset(tableinst, 0, sizeof(*tableinst));

                tableinst->elemtype = tt->elemtype;
                tableinst->length = tt->limits.min;
                tableinst->max = tt->limits.max;

                /* Fill in data pointer later */

                size_t idx = module_tables.n_elts;
                LVECTOR_GROW(&module_tables, 1);
                module_tables.elts[idx].offset = output->n_elts;
                module_tables.elts[idx].symidx = symbols->n_elts;
                module_tables.elts[idx].type = *tt;

                ADD_DATA_SYMBOL_STR(string_offset, sizeof(*tableinst));
                OUT(tableinst, sizeof(*tableinst));

                break;
            }
            case IMPORT_DESC_TYPE_MEM: {
                struct MemInst meminstv;
                struct MemInst *meminst = &meminstv;
                struct MemoryType *mt = &module->memory_section.memories[j].memtype;

                memset(meminst, 0, sizeof(*meminst));

                meminst->size = mt->limits.min * WASM_PAGE_SIZE;
                meminst->max = mt->limits.max * WASM_PAGE_SIZE;
                /* fill in data pointer later */

                size_t idx = module_mems.n_elts;
                LVECTOR_GROW(&module_mems, 1);
                module_mems.elts[idx].offset = output->n_elts;
                module_mems.elts[idx].symidx = symbols->n_elts;
                module_mems.elts[idx].type = *mt;

                ADD_DATA_SYMBOL_STR(string_offset, sizeof(*meminst));
                OUT(meminst, sizeof(*meminst));

                break;
            }
            case IMPORT_DESC_TYPE_GLOBAL: {
                struct GlobalInst globalinstv;
                struct GlobalInst *globalinst = &globalinstv;
                struct GlobalSectionGlobal *global = &module->global_section.globals[j];

                memset(globalinst, 0, sizeof(*globalinst));

                globalinst->value.type = global->type.valtype, globalinst->mut = global->type.mut;

                size_t idx = module_globals.n_elts;
                LVECTOR_GROW(&module_globals, 1);
                module_globals.elts[idx].symidx = symbols->n_elts;
                module_globals.elts[idx].type = global->type;

                ADD_DATA_SYMBOL_STR(string_offset, sizeof(*globalinst));
                OUT(globalinst, sizeof(*globalinst));

                break;
            }
            default:
                assert(0);
                break;
            }
        }
    }

    /* arrays of references to function instances */
#define OUTPUT_REF_ARRAY(_name, _type)                                                                                                                                                                                                                         \
    do {                                                                                                                                                                                                                                                       \
        size_t string_offset;                                                                                                                                                                                                                                  \
        string_offset = strtab->n_elts;                                                                                                                                                                                                                        \
        if (!output_buf(strtab, #_name, strlen(#_name) + 1))                                                                                                                                                                                                   \
            goto error;                                                                                                                                                                                                                                        \
        ADD_DATA_SYMBOL_STR(string_offset, module_##_name.n_elts * sizeof(_type));                                                                                                                                                                             \
        for (i = 0; i < module_##_name.n_elts; ++i) {                                                                                                                                                                                                          \
            _type ref = NULL;                                                                                                                                                                                                                                  \
            OUT(&ref, sizeof(ref));                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                      \
    } while (0)

    array_symbol_start = symbols->n_elts;
    funcs_offset = output->n_elts;
    OUTPUT_REF_ARRAY(funcs, struct FuncInst *);
    tables_offset = output->n_elts;
    OUTPUT_REF_ARRAY(tables, struct TableInst *);
    mems_offset = output->n_elts;
    OUTPUT_REF_ARRAY(mems, struct MemInst *);
    globals_offset = output->n_elts;
    OUTPUT_REF_ARRAY(globals, struct GlobalInst *);

#define OUTPUT_TYPE_ARRAY(_name, _type)                                                                                                                                                                                                                        \
    do {                                                                                                                                                                                                                                                       \
        size_t string_offset;                                                                                                                                                                                                                                  \
        string_offset = strtab->n_elts;                                                                                                                                                                                                                        \
        if (!output_buf(strtab, #_name "_types", strlen(#_name "_types") + 1))                                                                                                                                                                                 \
            goto error;                                                                                                                                                                                                                                        \
        ADD_DATA_SYMBOL_STR(string_offset, (n_imported_##_name##s) * sizeof(_type));                                                                                                                                                                           \
        for (i = 0; i < (n_imported_##_name##s); ++i) {                                                                                                                                                                                                        \
            _type ref;                                                                                                                                                                                                                                         \
            ref = (module_##_name##s).elts[i].type;                                                                                                                                                                                                            \
            OUT(&ref, sizeof(ref));                                                                                                                                                                                                                            \
        }                                                                                                                                                                                                                                                      \
    } while (0)

    types_symbol_start = symbols->n_elts;
    OUTPUT_TYPE_ARRAY(func, struct FuncType);
    OUTPUT_TYPE_ARRAY(table, struct TableType);
    OUTPUT_TYPE_ARRAY(mem, struct MemoryType);
    OUTPUT_TYPE_ARRAY(global, struct GlobalType);

    /* output global inits */
    global_inits_start = symbols->n_elts;
    ADD_DATA_SYMBOL(sizeof(struct GlobalInit) * module->global_section.n_globals);
    for (i = 0; i < module->global_section.n_globals; ++i) {
        struct GlobalSectionGlobal *global = &module->global_section.globals[i];
        struct GlobalInit global_init;

        if (global->n_instructions != 1)
            goto error;

        if (global->instructions[0].opcode == OPCODE_GET_GLOBAL) {
            /* fill in global pointer later */
            global_init.init_type = GLOBAL_GLOBAL_INIT;
        } else {
            global_init.init_type = GLOBAL_CONST_INIT;
            read_constant_expression(&global_init.init.constant, global->n_instructions, global->instructions);
        }

        module_globals.elts[i + n_imported_globals].init_symidx = symbols->n_elts;
        ADD_DATA_SYMBOL(sizeof(global_init));
        OUT(&global_init, sizeof(global_init));
    }

    /* add element data */
    element_symbol_start = symbols->n_elts;
    for (i = 0; i < module->element_section.n_elements; ++i) {
        struct ElementSectionElement *elt_sec = &module->element_section.elements[i];
        ADD_DATA_SYMBOL(elt_sec->n_funcidxs * sizeof(elt_sec->funcidxs[0]));
        OUTE(elt_sec->funcidxs, elt_sec->n_funcidxs, sizeof(elt_sec->funcidxs[0]));
    }

    element_struct_symbol_start = symbols->n_elts;
    ADD_DATA_SYMBOL(module->element_section.n_elements * sizeof(struct ElementInst));
    for (i = 0; i < module->element_section.n_elements; ++i) {
        struct ElementSectionElement *elt_sec = &module->element_section.elements[i];
        struct ElementInst elt;

        if (elt_sec->n_instructions != 1)
            goto error;

        if (elt_sec->instructions[0].opcode == OPCODE_GET_GLOBAL) {
            elt.offset_source_type = GLOBAL_GLOBAL_INIT;
        } else {
            elt.offset_source_type = GLOBAL_CONST_INIT;
            read_constant_expression(&elt.offset.constant, elt_sec->n_instructions, elt_sec->instructions);
        }

        elt.tableidx = elt_sec->tableidx;
        elt.n_funcidxs = elt_sec->n_funcidxs;

        ADD_DATA_PTR_RELOCATION(offsetof(struct ElementInst, funcidxs), &element_symbol_start);

        ADD_DATA_SYMBOL(sizeof(elt));

        OUT(&elt, sizeof(elt));
    }

    /* add wasm data */
    data_symbol_start = symbols->n_elts;
    for (i = 0; i < module->data_section.n_datas; ++i) {
        struct DataSectionData *data_sec = &module->data_section.datas[i];
        ADD_DATA_SYMBOL(data_sec->buf_size);
        OUT(data_sec->buf, data_sec->buf_size);
    }

    /* add wasm data structs */
    data_struct_symbol_start = symbols->n_elts;
    ADD_DATA_SYMBOL(module->data_section.n_datas * sizeof(struct DataInst));
    for (i = 0; i < module->data_section.n_datas; ++i) {
        struct DataSectionData *data_sec = &module->data_section.datas[i];
        struct DataInst data;

        if (data_sec->n_instructions != 1)
            goto error;

        if (data_sec->instructions[0].opcode == OPCODE_GET_GLOBAL) {
            data.offset_source_type = GLOBAL_GLOBAL_INIT;
        } else {
            data.offset_source_type = GLOBAL_CONST_INIT;
            read_constant_expression(&data.offset.constant, data_sec->n_instructions, data_sec->instructions);
        }
        data.memidx = data_sec->memidx;
        data.buf_size = data_sec->buf_size;

        ADD_DATA_PTR_RELOCATION(offsetof(struct DataInst, buf), &data_symbol_start);

        OUT(&data, sizeof(data));
    }

    /* add StaticModuleInst */
    {
        struct StaticModuleInst smi;

#define MEMBER_OFFSET(s, e) (((char *)(e)) - ((char *)(s)))

        smi.module.n_imported_funcs = n_imported_funcs;
        smi.module.n_imported_tables = n_imported_tables;
        smi.module.n_imported_mems = n_imported_mems;
        smi.module.n_imported_globals = n_imported_globals;
        smi.initted = 0;

        smi.module.types.n_elts = module->type_section.n_types;
        ADD_DATA_PTR_RELOCATION_RAW(output->n_elts + MEMBER_OFFSET(&smi, &smi.module.types.elts), type_inst_start);

        smi.module.funcs.n_elts = module_funcs.n_elts;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.module.funcs.elts), &array_symbol_start);

        smi.module.tables.n_elts = module_tables.n_elts;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.module.tables.elts), &array_symbol_start);

        smi.module.mems.n_elts = module_mems.n_elts;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.module.mems.elts), &array_symbol_start);

        smi.module.globals.n_elts = module_globals.n_elts;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.module.globals.elts), &array_symbol_start);

        smi.func_types.n_elts = n_imported_funcs;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.func_types.elts), &types_symbol_start);

        smi.table_types.n_elts = n_imported_tables;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.table_types.elts), &types_symbol_start);

        smi.mem_types.n_elts = n_imported_mems;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.mem_types.elts), &types_symbol_start);

        smi.global_types.n_elts = n_imported_globals;
        ADD_DATA_PTR_RELOCATION(MEMBER_OFFSET(&smi, &smi.global_types.elts), &types_symbol_start);

        smi.global_inits.n_elts = module_globals.n_elts - n_imported_globals;
        ADD_DATA_PTR_RELOCATION_RAW(output->n_elts + MEMBER_OFFSET(&smi, &smi.global_inits.elts), global_inits_start);

        smi.datas.n_elts = module->data_section.n_datas;
        ADD_DATA_PTR_RELOCATION_RAW(output->n_elts + MEMBER_OFFSET(&smi, &smi.datas.elts), data_struct_symbol_start);

        smi.elements.n_elts = module->element_section.n_elements;
        ADD_DATA_PTR_RELOCATION_RAW(output->n_elts + MEMBER_OFFSET(&smi, &smi.elements.elts), element_struct_symbol_start);

        smi.start_func = NULL;

        module_symbol = symbols->n_elts;
        { ADD_DATA_SYMBOL_OFF(MEMBER_OFFSET(&smi, &smi.module), sizeof(smi.module)); }

        static_module_symbol = symbols->n_elts;
        {
            size_t string_offset = strtab->n_elts;
            if (!output_buf(strtab, "module_inst", 12))
                goto error;
            ADD_DATA_SYMBOL_STR(string_offset, sizeof(smi));
        }

        OUT(&smi, sizeof(smi));

#undef MEMBER_OFFSET
    }

    /* compile codes */
    text_section_start = output->n_elts;

    /* output constructor */

    constructor_symbol = symbols->n_elts;
    {
        size_t code_size = 22;
        size_t string_offset = strtab->n_elts;
        if (!output_buf(strtab, "module_init", 12))
            goto error;
        ADD_FUNC_SYMBOL_STR(string_offset, code_size);

        ADD_FUNC_PTR_RELOCATION_RAW(output->n_elts + 2, static_module_symbol, 0);

        /* mov $const, %rdi */
        memcpy(buf, "\x48\xbf\x00\x00\x00\x00\x00\x00\x00\x00", 10);
        /* mov $const, %rax */
        memcpy(buf + 10, "\x48\xb8\x00\x00\x00\x00\x00\x00\x00\x00", 10);
        /* jmp *%rax */
        memcpy(buf + 20, "\xff\xe0", 2);
        if (!output_buf(output, buf, code_size))
            goto error;
    }

    module_types.functypes = malloc(module_funcs.n_elts * sizeof(struct FuncType));
    if (!module_types.functypes)
        goto error;
    module_types.tabletypes = malloc(module_tables.n_elts * sizeof(struct TableType));
    if (!module_types.tabletypes)
        goto error;
    module_types.memorytypes = malloc(module_mems.n_elts * sizeof(struct MemoryType));
    if (!module_types.memorytypes)
        goto error;
    module_types.globaltypes = malloc(module_globals.n_elts * sizeof(struct GlobalType));
    if (!module_types.memorytypes)
        goto error;

    for (i = 0; i < module_funcs.n_elts; ++i) {
        module_types.functypes[i] = module_funcs.elts[i].type;
    }
    for (i = 0; i < module_tables.n_elts; ++i) {
        module_types.tabletypes[i] = module_tables.elts[i].type;
    }
    for (i = 0; i < module_mems.n_elts; ++i) {
        module_types.memorytypes[i] = module_mems.elts[i].type;
    }
    for (i = 0; i < module_globals.n_elts; ++i) {
        module_types.globaltypes[i] = module_globals.elts[i].type;
    }

    func_code_start = symbols->n_elts;
    for (i = 0; i < module->function_section.n_typeidxs; ++i) {
        struct FuncType *ft = &module->type_section.types[module->function_section.typeidxs[i]];
        size_t code_size;
        void *code;
        struct MemoryReferences *memrefs;

        LVECTOR_GROW(&code_memrefs, 1);

        memrefs = &code_memrefs.elts[code_memrefs.n_elts - 1];
        memrefs->n_elts = 0;
        memrefs->elts = NULL;

        /* NB: we use the first memref to store information about
           the invoker */
        LVECTOR_GROW(memrefs, 1);

        code = wasmjit_compile_function(module->type_section.types, &module_types, ft, &module->code_section.codes[i], memrefs, &code_size, NULL, 0);
        if (!code)
            goto error;

        {
            size_t string_offset;
            int ret;
            string_offset = strtab->n_elts;
            ret = snprintf(buf, sizeof(buf), "function_code_%zu", i);
            if (!output_buf(strtab, buf, ret + 1))
                goto error;

            ADD_FUNC_SYMBOL_STR(string_offset, code_size);
        }

        OUT(code, code_size);

        code = wasmjit_compile_invoker_offset(ft, &memrefs->elts[0].code_offset, &code_size, 0);
        if (!code)
            goto error;

        {
            size_t string_offset;
            int ret;
            string_offset = strtab->n_elts;
            ret = snprintf(buf, sizeof(buf), "function_invoker_%zu", i);
            if (!output_buf(strtab, buf, ret + 1))
                goto error;

            ADD_FUNC_SYMBOL_STR(string_offset, code_size);
        }

        OUT(code, code_size);
    }

    /* add code references */
    for (i = n_imported_funcs; i < module_funcs.n_elts; ++i) {
        size_t off = module_funcs.elts[i].offset;

        ADD_DATA_PTR_RELOCATION_RAW(off + offsetof(struct FuncInst, module_inst), module_symbol);

        ADD_DATA_PTR_RELOCATION_RAW(off + offsetof(struct FuncInst, compiled_code), func_code_start + (i - n_imported_funcs) * 2);

        ADD_DATA_PTR_RELOCATION_RAW(off + offsetof(struct FuncInst, invoker), func_code_start + (i - n_imported_funcs) * 2 + 1);
    }

    text_section_end = output->n_elts;

    /* add bss references */
    /* align to 32 */
    if (output->n_elts % 32) {
        if (!output_buf(output, buf, 32 - output->n_elts % 32))
            goto error;
    }

    bss_section_start = output->n_elts;
    for (i = n_imported_tables; i < module_tables.n_elts; ++i) {
        size_t off = module_tables.elts[i].offset;
        size_t sidx = symbols->n_elts;
        size_t size = module_tables.elts[i].type.limits.min * sizeof(struct FuncInst *);

        if (!add_symbol(symbols, 0, STT_OBJECT, STB_LOCAL, STV_DEFAULT, BSS_SECTION_IDX, bss_size, size))
            goto error;

        ADD_DATA_PTR_RELOCATION_RAW(off + offsetof(struct TableInst, data), sidx);

        bss_size += size;
    }
    for (i = n_imported_mems; i < module_mems.n_elts; ++i) {
        size_t off = module_mems.elts[i].offset;
        size_t sidx = symbols->n_elts;
        size_t size = module_mems.elts[i].type.limits.min * WASM_PAGE_SIZE;

        if (!add_symbol(symbols, 0, STT_OBJECT, STB_LOCAL, STV_DEFAULT, BSS_SECTION_IDX, bss_size, size))
            goto error;

        ADD_DATA_PTR_RELOCATION_RAW(off + offsetof(struct MemInst, data), sidx);

        bss_size += size;
    }

    /* add external symbols */
    global_symbol_start = symbols->n_elts;

    init_static_module_symbol = symbols->n_elts;
    {
        size_t string_offset;
        string_offset = strtab->n_elts;
        if (!output_buf(strtab, "wasmjit_init_static_module", strlen("wasmjit_init_static_module") + 1))
            goto error;
        if (!add_symbol(symbols, string_offset, 0, STB_GLOBAL, 0, 0, 0, 0))
            goto error;
    }

    resolve_indirect_call_symbol = symbols->n_elts;
    {
        size_t string_offset;
        string_offset = strtab->n_elts;
        if (!output_buf(strtab, "wasmjit_resolve_indirect_call", strlen("wasmjit_resolve_indirect_call") + 1))
            goto error;
        if (!add_symbol(symbols, string_offset, 0, STB_GLOBAL, 0, 0, 0, 0))
            goto error;
    }

    trap_symbol = symbols->n_elts;
    {
        size_t string_offset;
        string_offset = strtab->n_elts;
        if (!output_buf(strtab, "wasmjit_trap", strlen("wasmjit_trap") + 1))
            goto error;
        if (!add_symbol(symbols, string_offset, 0, STB_GLOBAL, 0, 0, 0, 0))
            goto error;
    }

    /* add imported symbols */
#define ADD_IMPORTED_SYMBOLS(_name)                                                                                                                                                                                                                            \
    do {                                                                                                                                                                                                                                                       \
        for (i = 0; i < (n_imported_##_name##s); ++i) {                                                                                                                                                                                                        \
            int ret;                                                                                                                                                                                                                                           \
            size_t string_offset;                                                                                                                                                                                                                              \
            size_t import_idx = (module_##_name##s).elts[i].import_idx;                                                                                                                                                                                        \
            struct ImportSectionImport *import = &module->import_section.imports[import_idx];                                                                                                                                                                  \
                                                                                                                                                                                                                                                               \
            (module_##_name##s).elts[i].symidx = symbols->n_elts;                                                                                                                                                                                              \
                                                                                                                                                                                                                                                               \
            string_offset = strtab->n_elts;                                                                                                                                                                                                                    \
            ret = snprintf(buf, sizeof(buf), "%s__%s__" #_name, import->module, import->name);                                                                                                                                                                 \
            if (!output_buf(strtab, buf, ret + 1))                                                                                                                                                                                                             \
                goto error;                                                                                                                                                                                                                                    \
            if (!add_symbol(symbols, string_offset, 0, STB_GLOBAL, 0, 0, 0, 0))                                                                                                                                                                                \
                goto error;                                                                                                                                                                                                                                    \
        }                                                                                                                                                                                                                                                      \
    } while (0)

    ADD_IMPORTED_SYMBOLS(func);
    ADD_IMPORTED_SYMBOLS(table);
    ADD_IMPORTED_SYMBOLS(mem);
    ADD_IMPORTED_SYMBOLS(global);

    /* add imported global inits */
    for (i = 0; i < n_imported_globals; ++i) {
        int ret;
        size_t string_offset;
        size_t import_idx = module_globals.elts[i].import_idx;
        struct ImportSectionImport *import = &module->import_section.imports[import_idx];

        module_globals.elts[i].init_symidx = symbols->n_elts;

        string_offset = strtab->n_elts;
        ret = snprintf(buf, sizeof(buf), "%s_module", import->module);
        if (!output_buf(strtab, buf, ret + 1))
            goto error;
        if (!add_symbol(symbols, string_offset, 0, STB_GLOBAL, 0, 0, 0, 0))
            goto error;
    }

    /* we now have all module symbols, add back relocations */

#define ADD_REF_ARRAY_RELOCATIONS(_name, _type)                                                                                                                                                                                                                \
    do {                                                                                                                                                                                                                                                       \
        size_t ref_offset = (_name##_offset);                                                                                                                                                                                                                  \
        for (i = 0; i < (module_##_name).n_elts; ++i) {                                                                                                                                                                                                        \
            ADD_DATA_PTR_RELOCATION_RAW(ref_offset, (module_##_name).elts[i].symidx);                                                                                                                                                                          \
            ref_offset += sizeof(_type);                                                                                                                                                                                                                       \
        }                                                                                                                                                                                                                                                      \
    } while (0)

    /* add module array relocations */
    ADD_REF_ARRAY_RELOCATIONS(funcs, struct FuncInst *);
    ADD_REF_ARRAY_RELOCATIONS(tables, struct TableInst *);
    ADD_REF_ARRAY_RELOCATIONS(mems, struct MemInst *);
    ADD_REF_ARRAY_RELOCATIONS(globals, struct GlobalInst *);

    /* add global relocations */
    for (i = n_imported_globals; i < module_globals.n_elts; ++i) {
        struct GlobalSectionGlobal *global = &module->global_section.globals[i - n_imported_globals];
        size_t offset = symbols->elts[module_globals.elts[i].init_symidx].st_value;

        if (global->instructions[0].opcode != OPCODE_GET_GLOBAL)
            continue;

        if (global->instructions[0].data.get_global.globalidx >= n_imported_globals)
            /* can only refer to imported globals */
            goto error;

        ADD_DATA_PTR_RELOCATION_RAW(data_section_start + offset + offsetof(struct GlobalInit, init) + offsetof(union GlobalInitUnion, parent) + offsetof(struct MGStruct, module),
                                    module_globals.elts[global->instructions[0].data.get_global.globalidx].init_symidx);

        ADD_DATA_PTR_RELOCATION_RAW(data_section_start + offset + offsetof(struct GlobalInit, init) + offsetof(union GlobalInitUnion, parent) + offsetof(struct MGStruct, global),
                                    module_globals.elts[global->instructions[0].data.get_global.globalidx].symidx);
    }

    for (i = 0; i < module->element_section.n_elements; ++i) {
        size_t offset = symbols->elts[element_struct_symbol_start].st_value + i * sizeof(struct ElementInst);
        struct ElementSectionElement *elt_sec = &module->element_section.elements[i];

        if (elt_sec->instructions[0].opcode != OPCODE_GET_GLOBAL)
            continue;

        ADD_DATA_PTR_RELOCATION_RAW(data_section_start + offset + offsetof(struct ElementInst, offset) + offsetof(union ElementInstUnion, global), module_globals.elts[elt_sec->instructions[0].data.get_global.globalidx].symidx);
    }

    for (i = 0; i < module->data_section.n_datas; ++i) {
        size_t offset = symbols->elts[data_struct_symbol_start].st_value + i * sizeof(struct DataInst);
        struct DataSectionData *data_sec = &module->data_section.datas[i];

        if (data_sec->instructions[0].opcode != OPCODE_GET_GLOBAL)
            continue;

        ADD_DATA_PTR_RELOCATION_RAW(data_section_start + offset + offsetof(struct DataInst, offset) + offsetof(union DataInstUnion, global), module_globals.elts[data_sec->instructions[0].data.get_global.globalidx].symidx);
    }

    /* add startfunc reference */
    if (module->start_section.has_start) {
        ADD_DATA_PTR_RELOCATION_RAW(data_section_start + symbols->elts[static_module_symbol].st_value + offsetof(struct StaticModuleInst, start_func), module_funcs.elts[module->start_section.funcidx].symidx);
    }

    /* add constructor reference */
    ADD_FUNC_PTR_RELOCATION_RAW(text_section_start + symbols->elts[constructor_symbol].st_value + 12, init_static_module_symbol, 0);

    /* add code relocations */
    for (i = 0; i < module->function_section.n_typeidxs; ++i) {
        size_t j;
        size_t offset = symbols->elts[func_code_start + i * 2].st_value + text_section_start;
        struct MemoryReferences *memrefs = &code_memrefs.elts[i];

        /* NB: j = 1 to skip memref used for invoker */
        for (j = 1; j < memrefs->n_elts; ++j) {
            struct MemoryReferenceElt *elt = &memrefs->elts[j];
            size_t symidx;

            switch (elt->type) {
            case MEMREF_TYPE:
                symidx = type_inst_start + elt->idx;
                break;
            case MEMREF_FUNC:
                symidx = module_funcs.elts[elt->idx].symidx;
                break;
            case MEMREF_TABLE:
                symidx = module_tables.elts[elt->idx].symidx;
                break;
            case MEMREF_MEM:
                symidx = module_mems.elts[elt->idx].symidx;
                break;
            case MEMREF_GLOBAL:
                symidx = module_globals.elts[elt->idx].symidx;
                break;
            case MEMREF_RESOLVE_INDIRECT_CALL:
                symidx = resolve_indirect_call_symbol;
                break;
            case MEMREF_TRAP:
                symidx = trap_symbol;
                break;
            default:
                assert(0);
                __builtin_unreachable();
            }

            ADD_FUNC_PTR_RELOCATION_RAW(offset + elt->code_offset, symidx, 0);
        }

        /* add invoker relocation */
        {
            offset = symbols->elts[func_code_start + i * 2 + 1].st_value + text_section_start;
            struct MemoryReferenceElt *elt = &memrefs->elts[0];
            size_t symidx = func_code_start + i * 2;
            ADD_FUNC_PTR_RELOCATION_RAW(offset + elt->code_offset, symidx, 0);
        }
    }

    for (i = 0; i < module->export_section.n_exports; ++i) {
        char buf[0x100];
        struct ExportSectionExport *export = &module->export_section.exports[i];
        size_t sidx;
        size_t string_offset;
        int ret;
        char *type_string;

        switch (export->idx_type) {
        case IMPORT_DESC_TYPE_FUNC:
            sidx = module_funcs.elts[export->idx].symidx;
            type_string = "func";
            break;
        case IMPORT_DESC_TYPE_TABLE:
            sidx = module_tables.elts[export->idx].symidx;
            type_string = "table";
            break;
        case IMPORT_DESC_TYPE_MEM:
            sidx = module_mems.elts[export->idx].symidx;
            type_string = "mem";
            break;
        case IMPORT_DESC_TYPE_GLOBAL:
            sidx = module_globals.elts[export->idx].symidx;
            type_string = "global";
            break;
        default:
            assert(0);
            __builtin_unreachable();
            break;
        }

        string_offset = strtab->n_elts;
        ret = snprintf(buf, sizeof(buf), "%s__%s__%s", module_name, export->name, type_string);
        if (!output_buf(strtab, buf, ret + 1))
            goto error;

        Elf64_Sym *symbol = &symbols->elts[sidx];
        if (!add_symbol(symbols, string_offset, ELF64_ST_TYPE(symbol->st_info), STB_GLOBAL, symbol->st_other, symbol->st_shndx, symbol->st_value, symbol->st_size))
            goto error;
    }

    {
        size_t sidx = static_module_symbol;
        size_t string_offset = strtab->n_elts;
        Elf64_Sym *symbol = &symbols->elts[sidx];
        int ret;

        ret = snprintf(buf, sizeof(buf), "%s_module", module_name);
        if (!output_buf(strtab, buf, ret + 1))
            goto error;

        if (!add_symbol(symbols, string_offset, ELF64_ST_TYPE(symbol->st_info), STB_GLOBAL, symbol->st_other, symbol->st_shndx, symbol->st_value, symbol->st_size))
            goto error;
    }

    /* align to 8 */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    rela_text_section_start = output->n_elts;
    OUTE(text_relocations->elts, text_relocations->n_elts, sizeof(text_relocations->elts[0]));

    /* align to 8 */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    rela_data_section_start = output->n_elts;
    OUTE(data_relocations->elts, data_relocations->n_elts, sizeof(data_relocations->elts[0]));

    /* align to 8 */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    init_array_section_start = output->n_elts;
    memset(buf, 0, 8);
    OUT(buf, 8);

    /* align to 8 */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    rela_init_array_section_start = output->n_elts;
    {
        Elf64_Rela reloc = {
            .r_offset = 0,
            .r_info = ELF64_R_INFO(constructor_symbol, R_X86_64_64),
            .r_addend = 0,
        };
        OUT(&reloc, sizeof(reloc));
    }

    /* align to 8 */
    if (output->n_elts % 8) {
        if (!output_buf(output, buf, 8 - output->n_elts % 8))
            goto error;
    }

    symtab_section_start = output->n_elts;
    OUTE(symbols->elts, symbols->n_elts, sizeof(symbols->elts[0]));

    strtab_section_start = output->n_elts;
    OUT(strtab->elts, strtab->n_elts);

    shstrtab_section_start = output->n_elts;
    buf[0] = '\0';
    if (!output_buf(output, buf, 1))
        goto error;

    {
        text_sec.sh_name = output->n_elts - shstrtab_section_start;
        text_sec.sh_type = SHT_PROGBITS;
        text_sec.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        text_sec.sh_addr = 0;
        text_sec.sh_offset = text_section_start;
        text_sec.sh_size = text_section_end - text_section_start;
        text_sec.sh_link = 0;
        text_sec.sh_info = 0;
        text_sec.sh_addralign = 1;
        text_sec.sh_entsize = 0;
        memcpy(&output->elts[section_header_start + TEXT_SECTION_IDX * sizeof(Elf64_Shdr)], &text_sec, sizeof(text_sec));

        OUT(".text", strlen(".text") + 1);
    }

    {
        rela_text_sec.sh_name = output->n_elts - shstrtab_section_start;
        rela_text_sec.sh_type = SHT_RELA;
        rela_text_sec.sh_flags = SHF_INFO_LINK;
        rela_text_sec.sh_addr = 0;
        rela_text_sec.sh_offset = rela_text_section_start;
        rela_text_sec.sh_size = rela_data_section_start - rela_text_section_start;
        rela_text_sec.sh_link = SYMTAB_SECTION_IDX;
        rela_text_sec.sh_info = TEXT_SECTION_IDX;
        rela_text_sec.sh_addralign = 8;
        assert(24 == sizeof(text_relocations->elts[0]));
        rela_text_sec.sh_entsize = sizeof(text_relocations->elts[0]);
        memcpy(&output->elts[section_header_start + RELA_TEXT_SECTION_IDX * sizeof(Elf64_Shdr)], &rela_text_sec, sizeof(rela_text_sec));

        OUT(".rela.text", strlen(".rela.text") + 1);
    }

    {
        data_sec.sh_name = output->n_elts - shstrtab_section_start;
        data_sec.sh_type = SHT_PROGBITS;
        data_sec.sh_flags = SHF_WRITE | SHF_ALLOC;
        data_sec.sh_addr = 0;
        data_sec.sh_offset = data_section_start;
        data_sec.sh_size = text_section_start - data_section_start;
        data_sec.sh_link = 0;
        data_sec.sh_info = 0;
        data_sec.sh_addralign = 8;
        data_sec.sh_entsize = 0;
        memcpy(&output->elts[section_header_start + DATA_SECTION_IDX * sizeof(Elf64_Shdr)], &data_sec, sizeof(data_sec));

        OUT(".data", strlen(".data") + 1);
    }

    {
        rela_data_sec.sh_name = output->n_elts - shstrtab_section_start;
        rela_data_sec.sh_type = SHT_RELA;
        rela_data_sec.sh_flags = SHF_INFO_LINK;
        rela_data_sec.sh_addr = 0;
        rela_data_sec.sh_offset = rela_data_section_start;
        rela_data_sec.sh_size = init_array_section_start - rela_data_section_start;
        rela_data_sec.sh_link = SYMTAB_SECTION_IDX;
        rela_data_sec.sh_info = DATA_SECTION_IDX;
        rela_data_sec.sh_addralign = 8;
        assert(sizeof(data_relocations->elts[0]) == 24);
        rela_data_sec.sh_entsize = sizeof(data_relocations->elts[0]);
        memcpy(&output->elts[section_header_start + RELA_DATA_SECTION_IDX * sizeof(Elf64_Shdr)], &rela_data_sec, sizeof(rela_data_sec));

        OUT(".rela.data", strlen(".rela.data") + 1);
    }

    {
        bss_sec.sh_name = output->n_elts - shstrtab_section_start;
        bss_sec.sh_type = SHT_NOBITS;
        bss_sec.sh_flags = SHF_WRITE | SHF_ALLOC;
        bss_sec.sh_addr = 0;
        bss_sec.sh_offset = bss_section_start;
        bss_sec.sh_size = bss_size;
        bss_sec.sh_link = 0;
        bss_sec.sh_info = 0;
        bss_sec.sh_addralign = 32;
        bss_sec.sh_entsize = 0;
        memcpy(&output->elts[section_header_start + BSS_SECTION_IDX * sizeof(Elf64_Shdr)], &bss_sec, sizeof(bss_sec));

        OUT(".bss", strlen(".bss") + 1);
    }

    {
        init_array_sec.sh_name = output->n_elts - shstrtab_section_start;
        init_array_sec.sh_type = SHT_PROGBITS;
        init_array_sec.sh_flags = SHF_WRITE | SHF_ALLOC;
        init_array_sec.sh_addr = 0;
        init_array_sec.sh_offset = init_array_section_start;
        init_array_sec.sh_size = rela_init_array_section_start - init_array_section_start;
        init_array_sec.sh_link = 0;
        init_array_sec.sh_info = 0;
        init_array_sec.sh_addralign = 8;
        init_array_sec.sh_entsize = 8;
        memcpy(&output->elts[section_header_start + INIT_ARRAY_SECTION_IDX * sizeof(Elf64_Shdr)], &init_array_sec, sizeof(init_array_sec));

        OUT(".ctors", strlen(".ctors") + 1);
    }

    {
        rela_init_array_sec.sh_name = output->n_elts - shstrtab_section_start;
        rela_init_array_sec.sh_type = SHT_RELA;
        rela_init_array_sec.sh_flags = SHF_INFO_LINK;
        rela_init_array_sec.sh_addr = 0;
        rela_init_array_sec.sh_offset = rela_init_array_section_start;
        rela_init_array_sec.sh_size = symtab_section_start - rela_init_array_section_start;
        rela_init_array_sec.sh_link = SYMTAB_SECTION_IDX;
        rela_init_array_sec.sh_info = INIT_ARRAY_SECTION_IDX;
        rela_init_array_sec.sh_addralign = 8;
        rela_init_array_sec.sh_entsize = sizeof(Elf64_Rela);
        memcpy(&output->elts[section_header_start + RELA_INIT_ARRAY_SECTION_IDX * sizeof(Elf64_Shdr)], &rela_init_array_sec, sizeof(rela_init_array_sec));

        OUT(".rela.ctors", strlen(".rela.ctors") + 1);
    }

    {
        symtab_sec.sh_name = output->n_elts - shstrtab_section_start;
        symtab_sec.sh_type = SHT_SYMTAB;
        symtab_sec.sh_flags = 0;
        symtab_sec.sh_addr = 0;
        symtab_sec.sh_offset = symtab_section_start;
        symtab_sec.sh_size = strtab_section_start - symtab_section_start;
        symtab_sec.sh_link = STRTAB_SECTION_IDX;
        symtab_sec.sh_info = global_symbol_start;
        symtab_sec.sh_addralign = 8;
        assert(sizeof(symbols->elts[0]) == 24);
        symtab_sec.sh_entsize = sizeof(symbols->elts[0]);
        memcpy(&output->elts[section_header_start + SYMTAB_SECTION_IDX * sizeof(Elf64_Shdr)], &symtab_sec, sizeof(symtab_sec));

        OUT(".symtab", strlen(".symtab") + 1);
    }

    {
        strtab_sec.sh_name = output->n_elts - shstrtab_section_start;
        strtab_sec.sh_type = SHT_STRTAB;
        strtab_sec.sh_flags = 0;
        strtab_sec.sh_addr = 0;
        strtab_sec.sh_offset = strtab_section_start;
        strtab_sec.sh_size = shstrtab_section_start - strtab_section_start;
        strtab_sec.sh_link = 0;
        strtab_sec.sh_info = 0;
        strtab_sec.sh_addralign = 1;
        strtab_sec.sh_entsize = 0;
        memcpy(&output->elts[section_header_start + STRTAB_SECTION_IDX * sizeof(Elf64_Shdr)], &strtab_sec, sizeof(strtab_sec));

        OUT(".strtab", strlen(".strtab") + 1);
    }

    {
        shstrtab_sec.sh_name = output->n_elts - shstrtab_section_start;

        OUT(".shstrtab", strlen(".shstrtab") + 1);

        shstrtab_sec.sh_type = SHT_STRTAB;
        shstrtab_sec.sh_flags = 0;
        shstrtab_sec.sh_addr = 0;
        shstrtab_sec.sh_offset = shstrtab_section_start;
        shstrtab_sec.sh_size = output->n_elts - shstrtab_section_start;
        shstrtab_sec.sh_link = 0;
        shstrtab_sec.sh_info = 0;
        shstrtab_sec.sh_addralign = 1;
        shstrtab_sec.sh_entsize = 0;
        memcpy(&output->elts[section_header_start + SHSTRTAB_SECTION_IDX * sizeof(Elf64_Shdr)], &shstrtab_sec, sizeof(shstrtab_sec));
    }

    if (outsize)
        *outsize = output->n_elts;

    return output->elts;

error:
    if (module_types.functypes)
        free(module_types.functypes);
    if (module_types.tabletypes)
        free(module_types.tabletypes);
    if (module_types.memorytypes)
        free(module_types.memorytypes);
    if (module_types.globaltypes)
        free(module_types.globaltypes);
    /* TODO: cleanup code_memrefs */
    /* TODO: more cleanup */
    assert(0);

    return NULL;
}

#else

void *wasmjit_output_elf_relocatable(const char *module_name, const struct Module *module, size_t *outsize) {
    (void)module_name;
    (void)module;
    (void)outsize;
    return NULL;
}

#endif
