#include <iostream>
#include <memory>
#include <fstream>
#include <string>

#include "Include/peglib.h"
#include "Interpreter.hpp"
#include "Indent.hpp"

#define CERROR(cond,str) if(cond){std::cerr<<str<<std::endl;return EXIT_FAILURE;}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cerr << argv[0] << " {file}.py" << std::endl;
        return EXIT_FAILURE;
    }
    auto src = argv[1];
    std::ifstream inputStream(src, std::ios::in);
    std::ofstream traceFile("trace.log", std::ios::out);
    std::ofstream varHistFile("varhistory.log", std::ios::out);
    std::ofstream errorFile("error.log", std::ios::out);
    traceFile << "Source argument: " << src << std::endl;
    
    // Define grammar.
    // https://bford.info/pub/lang/peg.pdf
    auto grammar = (R"(
        program         <- (NEWLINE / Comment / function / stmt / indent_block)+ EOF
        
        indent_block    <- NEWLINE* _ '{' block NEWLINE* _ '}' NEWLINE* 
        block           <-  (indent_block / statement)+ { no_ast_opt }
        function        <- ('def' __ NAME __'(' _ Args(NAME)? ')' __ ':' indent_block)

        stmt            <- (while / if / Comment / list_expr / assignment / call) ';'?
        statement       <- NEWLINE? Samedent (while / if / NEWLINE / Comment / list_expr / assignment / call / return_stmt) ';'?

        list_expr       <- list_assign / list_create
        list_assign     <- (NAME '[' _ (list_op / expression) _ ']' _ '=' _ expression)
        list_create     <- NAME '=' _ '[' _ Args(expression)? ']' _ !term_op { no_ast_opt }
        assignment      <- NAME '=' _ expression
        call            <- NAME '(' _ Args(call / VALUE / expression)? ')' _ { no_ast_opt }

        if              <- 'if' __ compare ':' _ indent_block _ ('else' ':' indent_block)?
        compare         <-  (compare_prefix VALUE) / ((VALUE compare_infix ' '* VALUE)) / ('(' (VALUE compare_infix ' '* VALUE) ')')
        compare_prefix  <- 'not'
        compare_infix   <- '==' / '<=' / '>=' / '<' / '>' / 'and' / 'or'

        while           <- 'while' __ '(' _ compare _ ')' _ ':'  indent_block
        return_stmt     <- 'return' _ expression { no_ast_opt }

        expression      <- sign term (term_op term)*
        sign            <- < [-+]? > _
        term_op         <- < [-+] > _
        term            <- factor (factor_op factor)*
        factor_op       <- < [*/] > _
        factor          <- VALUE / '(' _ expression ')' _
        VALUE           <- raw_list / list_value / call / STRING / NAME / NUMBER
        
        raw_list        <- _ '[' _ Args(expression / VALUE)? ']' _ { no_ast_opt }
        list_value      <- NAME '[' _ (':'/ list_op) ']' _
        list_op         <- list_splice / NUMBER / NAME
        list_splice     <- leftSp? ':' rightSp? { no_ast_opt }
        leftSp          <- expression { no_ast_opt }
        rightSp         <- expression { no_ast_opt }
        
        
        keyword         <- 'while' / 'if' / 'def'
        
        STRING          <- '"' < (!'"' .)* > '"'
        NAME            <- !keyword < [a-zA-Z] [a-zA-Z0-9]* > _
        NUMBER          <- < [0-9]+ > _


        ~Samedent        <- (' ')* {}
        Args(x)         <- x _ (',' _ x)*
        ~Comment        <- '#' [^\r\n]* _
        ~NEWLINE        <- [\r\n]+
        ~_              <- [ \t]*
        ~__             <- ![a-z0-9_] _
        ~EOF            <- !.
    )");


    peg::parser parser(grammar);

    // size_t indent = 0;
    // parser["block"].enter = [&](const Context & /*c*/, const char * /*s*/,
    //                             size_t /*n*/, std::any & /*dt*/) { indent += 2; };

    // parser["block"].leave = [&](const Context & /*c*/, const char * /*s*/,
    //                             size_t /*n*/, size_t /*matchlen*/,
    //                             std::any & /*value*/,
    //                             std::any & /*dt*/) { indent -= 2; };

    // parser["Samedent"].predicate =
    //     [&](const SemanticValues &vs, const std::any & /*dt*/, std::string &msg) {
    //         if (indent != vs.sv().size()) {
    //         msg = "different indent...";
    //         return false;
    //         }
    //         return true;
    //     };

    CERROR(parser!=true, "Could not generate a parser from defined grammar.");
    CERROR(inputStream.fail(), "Could not open source file");

    std::stringstream buffer;
    buffer << inputStream.rdbuf();
    std::string source = pythonCFL(buffer.str());
    traceFile << "---- BEG INPUT ----" << std::endl;
    traceFile << source << std::endl;
    traceFile << "---- END INPUT ----" << std::endl;
    
    parser.set_logger([&](size_t line, size_t col, const std::string& msg, const std::string &rule) {
        std::string errMsg = std::to_string(line) + ":" + std::to_string(col) + ": " + msg + " | rule: " + rule + "\n";
        errorFile << errMsg;
        std::cerr << errMsg;
    });

    parser.enable_ast();
    parser.enable_packrat_parsing();
    std::shared_ptr<peg::Ast> ast;
    if(parser.parse(source, ast)) {
        ast = parser.optimize_ast(ast);
        traceFile << peg::ast_to_s(ast);
        traceFile << "----" << std::endl;
        try {
            interpret(ast, std::cout, traceFile, varHistFile, errorFile);
        } catch(const std::exception& e) {
            std::cerr << e.what() << std::endl;
            errorFile << e.what() << std::endl;
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    errorFile << "Syntax error, could not parse" << std::endl;
    return EXIT_FAILURE;
}