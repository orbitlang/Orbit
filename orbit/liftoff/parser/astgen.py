import datetime
import os

HEADER = """// This source file is part of the Orbit project.
//
// Licensed under the Apache License v2.0
"""

GUARD_START = "#ifndef ORBIT_LIFTOFF_PARSER_AST_H_\n#define ORBIT_LIFTOFF_PARSER_AST_H_\n"
GUARD_END = "#endif // !ORBIT_LIFTOFF_PARSER_AST_H_"

INCLUDES = """#include <cassert>
#include <vector>

#include <orbit/orbiter/datatype/orstring.h>
#include <orbit/orbiter/memory/memory.h>

#include <orbit/liftoff/scanner/token.h>
#include <orbit/liftoff/symtable.h>
"""

NAMESPACE = "namespace liftoff::parser"

BASE_CLASS = "ASTNode"

NODES = {
    "ASTNode": {
        "fields": {
            "node_type": "NodeType",
            "loc": "scanner::Loc"
        }
    },
    "Assignment": {
        "fields": {
            "token_type": "scanner::TokenType",
            "name": "ASTNode*",
            "value": "ASTNode*"
        }
    },
    "Binary": {
        "fields": {
            "token_type": "scanner::TokenType",
            "left": "ASTNode*",
            "right": "ASTNode*"
        },
        "node_type": ["BINARY", "ELVIS", "IN", "NOT_IN", "NULL_COALESCING", "SELECTOR"]
    },
    "Branch": {
        "fields": {
            "test": "ASTNode*",
            "body": "ASTNode*",
            "orelse": "ASTNode*"
        }
    },
    "Identifier": {
        "fields": {
            "value": "orbiter::datatype::ORString*"
        }
    },
    "ListExpression": {
        "fields": {
            "elements": "std::vector<ASTHandle<ASTNode*>>"
        },
        "node_type": ["DICT", "LIST", "SET", "TUPLE"]
    },
    "Literal": {
        "fields": {
            "literal": "orbiter::datatype::OObject*"
        }
    },
    "Module": {
        "fields": {
            "filename": "orbiter::datatype::ORString*",
            "filepath": "orbiter::datatype::ORString*",
            "docstring": "orbiter::datatype::ORString*",
            "statements": "std::vector<ASTHandle<ASTNode*>>",
            "sym_t": "SymbolTable*"
        }
    },
    "Subscript": {
        "fields": {
            "expression": "ASTNode*",
            "start": "ASTNode*",
            "stop": "ASTNode*",
            "step": "ASTNode*"
        },
        "node_type": ["INDEX", "SLICE"]
    },
    "Unary": {
        "fields": {
            "token_type": "scanner::TokenType",
            "value": "ASTNode*"
        },
        "node_type": ["AWAIT", "NIL_SAFE", "PANIC", "SPAWN", "TRAP", "UNARY", "UPDATE"]
    }
}


def generate_custom_content():
    return """"""


def generate_node_enum():
    enum_values = []
    for node_name, node_info in NODES.items():
        if node_name == "ASTNode":
            continue
        if "node_type" in node_info:
            for name in node_info["node_type"]:
                enum_values.append(f"    {name},")
        else:
            enum_values.append(f"    {node_name.upper()},")
    return "enum class NodeType {\n" + "\n".join(enum_values) + "\n};\n\n"


def generate_fields(fields):
    return "\n    ".join(f"{type} {name};" for name, type in fields.items())


def generate_cleanup(fields):
    cleanup_lines = []
    for name, type in fields.items():
        if type == "orbiter::datatype::OHandle":
            cleanup_lines.append(f"        node->{name}.~Handle();")
        elif type.startswith("orbiter::datatype::"):
            cleanup_lines.append(f"        Release(node->{name});")
        elif type.startswith("std::vector"):
            cleanup_lines.append(f"        node->{name}.~vector();")
        elif type.startswith("SymbolTable"):
            cleanup_lines.append(f"        SymbolTableDel(node->{name});")
        elif type.endswith("ASTNode*") or type in NODES:
            cleanup_lines.append(f"        if (node->{name})")
            cleanup_lines.append(f"            ASTNodeCleanup(node->{name});")
    if cleanup_lines:
        return "\n" + "\n".join(cleanup_lines)
    return ""


def generate_node(name, fields):
    inheritance = f": {BASE_CLASS}" if name != BASE_CLASS else ""
    return f"""struct {name}{inheritance} {{
    {generate_fields(fields['fields'])}
}};
"""


def generate_cleanup_function():
    cleanup_cases = []
    for name, fields in NODES.items():
        if name == "ASTNode":
            continue
        cleanup_body = generate_cleanup(fields['fields'])
        if cleanup_body:
            if 'node_type' in fields:
                for node_type in fields['node_type']:
                    cleanup_cases.append(f"""        case NodeType::{node_type}:""")
                cleanup_cases.append(f"""        {{
                auto* node = ({name}*)ast_node;{cleanup_body}
                break;
            }}""")
            else:
                cleanup_cases.append(f"""        case NodeType::{name.upper()}: {{
                auto* node = ({name}*)ast_node;{cleanup_body}
                break;
            }}""")

    return f"""inline void ASTNodeCleanup(ASTNode* ast_node) {{
    if (ast_node == nullptr) return;
    
    switch (ast_node->node_type) {{
{' '.join(cleanup_cases)}
        default:
            assert(false && "Unknown node type");
            break;
    }}
    
    orbiter::memory::Free(ast_node);
}}
"""


def generate_make_functions():
    make_functions = []
    for node_name, node_info in NODES.items():
        if node_name == "ASTNode":
            continue

        vector_initializations = []
        for field_name, field_type in node_info['fields'].items():
            if field_type.startswith("std::vector"):
                vector_initializations.append(f"        new (&node->{field_name}) {field_type}();")

        vector_init_code = "\n".join(vector_initializations)

        if 'node_type' in node_info:
            node_types = node_info['node_type']
            node_type_check = " || ".join(f"node_type == NodeType::{nt}" for nt in node_types)
            make_functions.append(f"""
inline ASTHandle<{node_name}*> Make{node_name}(const scanner::Loc &loc, NodeType node_type) {{
    assert({node_type_check});
    auto *node = ({node_name} *) orbiter::memory::Calloc(sizeof({node_name}));
    if(node != nullptr) {{
        node->node_type = node_type;
        node->loc = loc;
        
{vector_init_code}
    }}
    return ASTHandle(node);
}}
""")
        else:
            make_functions.append(f"""
inline ASTHandle<{node_name}*> Make{node_name}(const scanner::Loc &loc) {{
    auto *node = ({node_name} *) orbiter::memory::Calloc(sizeof({node_name}));
    if(node != nullptr) {{
        node->node_type = NodeType::{node_name.upper()};
        node->loc = loc;
        
{vector_init_code}
    }}
    return ASTHandle(node);
}}
""")
    return "\n".join(make_functions)


def generate_ast():
    return "\n".join(generate_node(name, fields) for name, fields in NODES.items())


def generate_ast_handle():
    return """template<typename T>
    class ASTHandle {
        static_assert(std::is_pointer_v<T>, "T must be a pointer type");
        static_assert(std::is_base_of_v<struct ASTNode, std::remove_pointer_t<T> >,
                      "T must be a pointer to ASTNode or a type derived from ASTNode");

        T node_;

    public:
        ASTHandle() noexcept : node_(nullptr) {
        }

        explicit ASTHandle(T node) noexcept : node_(node) {
        }

        ASTHandle(const ASTHandle &) = delete;

        ASTHandle(ASTHandle &&other) noexcept : node_(other.node_) {
            other.node_ = nullptr;
        }

        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U, T> > >
        ASTHandle(ASTHandle<U> &&other) noexcept : node_(other.release()) {
        }

        ~ASTHandle() noexcept {
            this->reset();
        }

        ASTHandle &operator=(const ASTHandle &) = delete;

        ASTHandle &operator=(ASTHandle &&other) noexcept {
            if (this != &other) {
                this->reset();

                this->node_ = other.node_;

                other.node_ = nullptr;
            }

            return *this;
        }

        template<typename U, typename = std::enable_if_t<std::is_convertible_v<U, T> > >
        ASTHandle &operator=(ASTHandle<U> &&other) noexcept {
            this->reset();

            this->node_ = static_cast<T>(other.release());

            return *this;
        }

        explicit operator bool() const noexcept { return this->node_ != nullptr; }

        std::remove_pointer_t<T> &operator*() const { return *this->node_; }

        T operator->() const noexcept { return this->node_; }

        [[nodiscard]] T get() const noexcept { return this->node_; }

        T release() noexcept {
            T temp = this->node_;
            
            this->node_ = nullptr;

            return temp;
        }

        void reset() noexcept {
            if (this->node_ != nullptr) {
                ASTNodeCleanup(this->node_);
                
                this->node_ = nullptr;
            }
        }
    };
"""


def generate_visitor_base():
    visit_cases = []
    visit_methods = []

    for name, node_info in NODES.items():
        if name == "ASTNode":
            continue

        if 'node_type' in node_info:
            for node_type in node_info['node_type']:
                visit_cases.append(f"case NodeType::{node_type}:")
            visit_cases.append(f"    return static_cast<Derived*>(this)->visit{name}(({name} *) node);")
        else:
            visit_cases.append(
                f"case NodeType::{name.upper()}: return static_cast<Derived*>(this)->visit{name}(({name} *) node);")

        visit_methods.append(f"ASTNode* visit{name}({name}* node) {{ return node; }}")

    visit_cases_str = "\n        ".join(visit_cases)
    visit_methods_str = "\n\n    ".join(visit_methods)

    return f"""
template <typename Derived>
struct ASTVisitor {{
    ASTNode* visit(ASTNode* node) {{
        switch(node->node_type) {{
        {visit_cases_str}
        default: assert(false); return nullptr;
        }}
    }}
    
    {visit_methods_str}
}};"""


def create_visitor(name):
    methods = "\n    ".join(f"ASTNode* visit{node}({node}* node);" for node in NODES.keys())
    return f"""
struct {name} : ASTVisitor<{name}> {{
    {methods}
}};
"""


def generate_file_content(visitor_names):
    current_date = datetime.datetime.now().strftime("%Y-%m-%d")
    content = [
        HEADER,
        f"// Created by ASTGen: {current_date}\n// DO NOT EDIT THIS FILE\n",
        GUARD_START,
        INCLUDES,
        f"{NAMESPACE} {{",
        generate_node_enum(),
        generate_ast_handle(),
        generate_ast(),
        generate_cleanup_function(),
        generate_visitor_base(),
        generate_make_functions(),
        generate_custom_content()
    ]

    for visitor_name in visitor_names:
        content.append(f"\n// {visitor_name} visitor")
        content.append(create_visitor(visitor_name))

    content.extend([
        "} // namespace liftoff::parser",
        GUARD_END
    ])

    return "\n".join(content)


def generate_visitor_cpp(name):
    implementations = []
    for node in NODES.keys():
        implementations.append(f"""
ASTNode* {name}::visit{node}({node}* node) {{
    // TODO: Implement {node} visitation
    return node;
}}""")

    return HEADER + f"""
#include <orbit/liftoff/parser/ast.h>

namespace liftoff::parser {{
{' '.join(implementations)}
}} // namespace liftoff::parser
"""


if __name__ == "__main__":
    visitor_names = []

    with open("ast.h", "w") as f:
        f.write(generate_file_content(visitor_names))
    print("AST header generated successfully!")

    # Gen visitor
    for visitor_name in visitor_names:
        cpp_filename = f"{visitor_name.lower()}.cpp"
        if not os.path.exists(cpp_filename):
            with open(cpp_filename, "w") as f:
                f.write(generate_visitor_cpp(visitor_name))
            print(f"Custom visitor implementation file {cpp_filename} created")
        else:
            print(f"File {cpp_filename} already exists, skipping creation")
