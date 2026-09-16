# liftoff: the compiler (scanner, parser, IR, register allocation, codegen).

target_sources(Orbiter PRIVATE
        codegen.cpp
        compiler.cpp
        optimizer.cpp
        symtable.cpp

        ir/builder.cpp
        ir/intervalspiller.cpp
        ir/irbuilder.cpp
        ir/ircontext.cpp
        ir/jblock.cpp
        ir/linearscan.cpp

        parser/parser.cpp

        scanner/ibuffer.cpp
        scanner/sbuffer.cpp
        scanner/scanner.cpp)
