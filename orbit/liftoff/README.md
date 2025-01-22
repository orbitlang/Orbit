# LiftOff: The Orbit Compiler

LiftOff is the default implementation of the compiler for the Orbit programming language, supporting all the language features.

## Project Structure

The LiftOff compiler is divided into several main components:

- **Scanner**: Responsible for lexical analysis, breaking down the source code into tokens.
- **Parser**: Performs syntactic analysis, constructing an Abstract Syntax Tree (AST) from the tokens.
- **IR (Intermediate Representation)**:
    - **IRBuilder**: Converts the AST into an intermediate representation
    - **LinearScan**: Performs register allocation
    - **IRContext**: The base object that contains the intermediate representation
- **codegen.cpp**: Generates the actual bytecode for the OrbiterVM
- **compiler.cpp**: Orchestrates all the compilation phases to produce the final Code Object

The compilation pipeline follows these steps:
1. Source code is scanned into tokens
2. Tokens are parsed into an AST
3. AST is converted to IR
4. IR Optimization
5. IR undergoes register allocation
6. Finally, bytecode is generated for the VM

The `grammar.ebnf` file in the directory contains the formal grammar specification for the Orbit language.
