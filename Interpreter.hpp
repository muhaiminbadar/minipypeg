#pragma once

#include <string>
#include <functional>
#include <variant>
#include <memory>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <fstream>

#include "Include/peglib.h"

using std::string;
using std::function;
using std::vector;
using std::nullptr_t;
using std::shared_ptr;

using namespace peg;
using namespace peg::udl;

std::ostream* traceLog;
std::ostream * varLog;
std::ostream* errorLog;


// Original intepretor credit: yhirose; modified to work w/ python.
struct Value;
using List = vector<Value>;
using Function = function<Value(List&)>;

// The class that will hold all our interpreter values. Value can take any of the defined forms below. 
struct Value {
    std::variant<nullptr_t, bool, long, string, Function, List> v;
    Value() 
        : v(nullptr) {}

    explicit Value(bool b) 
        : v(b) {}
    explicit Value(long l) 
        : v(l) {}
    explicit Value(string s) 
        : v(s) {}
    explicit Value(Function f) 
        : v(std::move(f)) {}
    explicit Value(List l) 
        : v(std::move(l)) {}


    // Fetch
    template<typename T>
    T get() const {
        try {
            return std::get<T>(v);
        } catch (const std::bad_variant_access& e) {
            string msg = "TypeError: Got unexpected type " + getTypeName(v.index());
            std::cerr << e.what() << "[" << typeid(T).name() << ", " << v.index() << "]" << std::endl;
            *errorLog << msg;

            throw std::runtime_error(msg);
        }
    }
    // Equality check.
    bool operator==(const Value& rhs) const {
        switch (v.index()) {
            case 0:
                return get<nullptr_t>() == rhs.get<nullptr_t>();
            case 1:
                return get<bool>() == rhs.get<bool>();
            case 2:
                return get<long>() == rhs.get<long>();
            case 3:
                return get<string>() == rhs.get<string>();
            case 5:
                return get<List>() == rhs.get<List>();
        }
    }
    // For printing
    static string getTypeName(int type) {
        switch(type) {
            case 0:
                return "None";
            case 1:
                return "bool";
            case 2:
                return "int";
            case 3:
                return "string";
            case 4:
                return "function";
            case 5: 
                return "list";
        }
        return "Unknown";
    }
    // For printing
    string str() const {
        switch (v.index()) {
            case 0:
                return "nil";
            case 1:
                return get<bool>() ? "true" : "false";
            case 2:
                return std::to_string(get<long>());
            case 3:
                return string(get<string>());
            case 4:
                return "Function";
            case 5: {
                string out = "[";
                auto list = get<List>();
                for(int i = 0; i < list.size() - 1; i ++) {
                    if(list[i].v.index() != 0)
                        out = out + list[i].str() + ", ";
                }
                out = out + list.back().str() + "]";
                return out;
            }
        }
        return "?";
    }
};

// Environment class, which will function akin to a "stack" or symbol table where everything is kept.
struct Env {
    std::shared_ptr<Env> outer;
    std::unordered_map<string, Value> values;

    Env(shared_ptr<Env> outer = nullptr) 
        : outer(outer) {}

    Value get_value(string s) const {
        *varLog << "- reading symbol: " << s << " at " << this << std::endl;
        if (auto it = values.find(s); it != values.end()) {
            return Value(it->second);
        } else if (outer) {
            return Value(outer->get_value(s));
        }
        throw std::runtime_error("undefined symbol '" + string(s) + "'...");
    }
    void set_value(string s, const Value& val) { 
        *traceLog << "(" << this << ") Assigning " << s << " = " << val.str() << std::endl;
        *varLog << "(" << this << ") Assigning " << s << " = " << val.str() << std::endl;
        values[s] = Value(val); 
    }
};

// Interpreter:
Value eval(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env);
Value eval_call(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    auto fn = env->get_value(ast->nodes[0]->token_to_string()).get<Function>();
    List values; 
    for(int i = 1u; i < ast->nodes.size(); i += 1) { // Push all the arguments for the call.
        values.push_back(eval(ast->nodes[i], env));
    }
    return fn(values); // Call the function and return.
}
Value eval_assign(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    auto name = ast->nodes[0]->token_to_string(); // Lhs
    auto value = eval(ast->nodes[1], env); // Rhs
    env->set_value(name, value); // Apply to symbol table.
    return Value(); 
}
Value eval_block(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    for(auto node : ast->nodes) {
        if(node->tag == "return_stmt"_) // encounter return in the block, no need to continue executing!
        {
            Value v(eval(node, env));
            *traceLog << "returning " << Value::getTypeName(v.v.index()) << " " << v.str() << std::endl;
            return v;
        }
        else if(node->tag == "if"_) { // If return was called in a nested block, we need to check
            Value v = eval(node, env);
            if(v.v.index() != 0)
                return v;
        } else { // Otherwise execute next statement in the block
            eval(node, env);
        }
    }
    return Value();
}
Value eval_expr(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    // Expression can be in many defined forms. For operator overload we must check what context we are in by checking the AST tags.
    const auto& nodes = ast->nodes;
    auto sign = nodes[0]->token_to_string();

    if(nodes.size() == 2) { 
        if(nodes[1]->tag == "call"_ || nodes[1]->tag == "list_value"_)
            return eval(nodes[1], env);
        else if(nodes[1]->tag == "STRING"_)
            return Value(nodes[1]->token_to_string());
    }
    
    // Evaluate overloaded concatenation list expression starting with [] list
    if(nodes[1]->tag == "raw_list"_) {
        List master;
        for(auto k : ast->nodes[1]->nodes) {
            master.push_back(eval(k, env));
        }
        for(auto i = 2; i < nodes.size(); i += 2) {
            if(ast->nodes[i]->token_to_string()[0] == '+') {
                if(ast->nodes[i+1]->tag == "raw_list"_) {
                    for(auto k : ast->nodes[i+1]->nodes) {
                        master.push_back(eval(k, env));
                    }
                }
                else {
                    List l2 = env->get_value(ast->nodes[i+1]->token_to_string()).get<List>();
                    for(auto k : l2) {
                        if(k.v.index() != 0)
                            master.push_back(k);
                    }
                }
            }
        }

        return Value(master);
    } 
    else if(nodes[1]->tag == "NAME"_) {
        auto val = env->get_value(nodes[1]->token_to_string());
        if(val.v.index() == 5) { // List expression starting with a variable
            List master = val.get<List>();
            for(auto i = 2; i < nodes.size(); i += 2) {
                if(ast->nodes[i]->token_to_string()[0] == '+') {
                    if(ast->nodes[i+1]->tag == "raw_list"_) { // next term is raw_list
                        for(auto k : ast->nodes[i+1]->nodes) {
                            master.push_back(eval(k, env));
                        }
                    } else { // next term is list variable
                        List l2 = env->get_value(ast->nodes[i+1]->token_to_string()).get<List>();

                        for(auto k : l2) {
                            if(k.v.index() != 0)
                                master.push_back(k);
                        }
                    }
                }
            }
            return Value(master);
        }
        else if (val.v.index() == 3) // string concat
        {
            string result = val.get<string>();
            for(auto i = 2; i < nodes.size(); i += 2) {
                if(ast->nodes[i]->token_to_string()[0] == '+') {
                    auto s2 = eval(ast->nodes[i+1], env);
                    result = result + s2.get<string>();
                }
            }
            return Value(result);
        }
    } 

    // Regular arithmetic expression.
    long sign_val = (sign.empty() || sign == "+") ? 1 : -1;
    long val = eval(nodes[1], env).get<long>() * sign_val;
    for(auto i = 2u; i < nodes.size(); i += 2) {
        auto oper = nodes[i + 0]->token_to_string()[0];
        long rval = eval(nodes[i + 1], env).get<long>();
        switch(oper) {
            case '+':
                val = val + rval;
                break;
            case '-':
                val = val - rval;
                break;
        }
    }

    return Value(val);
}
Value eval_term(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) { // Evaluate term
    const auto& nodes = ast->nodes;
    long val = eval(nodes[0], env).get<long>();
    for(auto i = 1u; i < nodes.size(); i += 2) {
        auto oper = nodes[i + 0]->token_to_string()[0];
        long rval = eval(nodes[i + 1], env).get<long>();
        switch(oper) {
            case '*':
                val = val * rval;
                break;
            case '/':
                if(rval == 0)
                    throw std::runtime_error("Divide by zero");
                val = val / rval;
                break;
        }
    }
    return Value(val);
}
Value declare_function(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    string name = ast->nodes[0]->token_to_string();

    // Setup function with values that are passed to it. The actual evaluation will happen in the function block with the parameters set here.
    auto fxn = Value(Function([=](const List& values) {
    
        shared_ptr<Env> context = std::make_shared<Env>(env); // Setup function's own symbol table
        for(auto i = 0; i < values.size(); i ++) { // Assign function call values passed as a vector.
            auto str = ast->nodes[1+i]->token_to_string();
            *traceLog << "- assign fxn " << name << " value " << str << " to: " << values[i].str() << std::endl;
            context->set_value(str, Value(values[i])); // assign them to our defined symbol table
        }
        *traceLog << "-- executing " << name << "  ---" << std::endl;
        auto block = ast->nodes.back(); // get block address
        auto v = eval(block, context); // execute the function value
        *traceLog << "-- end func " << name << ", rtn: " << Value::getTypeName(v.v.index()) << std::endl;
        return v;
    }));

    env->set_value(name, fxn);
    return Value();
}

Value declare_list(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    const auto& nodes = ast->nodes;
    string name = ast->nodes[0]->token_to_string(); // non-empty list define
    if(nodes.size() > 1) {
        env->set_value(name, Value([&]{
            List temp;
            for(auto i = 1u; i < nodes.size(); i += 1) {
                temp.push_back(eval(nodes[i], env));
            }
            return std::move(temp); 
        }()));
    } else { // empty list defined
        List t;
        t.resize(1);
        env->set_value(name, Value(t));
    }

    return Value();
}
Value access_list(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    string name = ast->nodes[0]->token_to_string();
    auto vList = env->get_value(name).get<List>();
    // Accessing a spliced list
    if(ast->nodes[1]->tag == "list_splice"_) {
        auto& iNodes = ast->nodes[1]->nodes;
        long l = -1;
        long r = -1;
        for(auto k : iNodes) {
            if(k->tag == "leftSp"_)
                l = eval(k, env).get<long>();
            else if(k->tag == "rightSp"_)
                r = eval(k, env).get<long>();
        }
        if(l != -1 && r == -1) { // list[x:]
            r = vList.size();
        } else if(l == -1 && r != -1) { // list[:x]
            l = 0;
        } else if(l != -1 && r != -1) { // list[x:x]
            // continue.
        } else { // list[:]
            l = 0;
            r = vList.size();
        }  

        List t;
        for(int i = l; i < r; i ++)
            t.push_back(vList[i]);
        return Value(t);
    }
    else { // Non splice list.
        auto index = eval(ast->nodes[1], env).get<long>();
        *traceLog << "Get list value from " << name  << " at " << index << std::endl;
        
        if(index < 0 || index >= vList.size())
            throw std::runtime_error("Accessing invalid element");

        return vList[index];
    }
}

Value list_assign(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    string name = ast->nodes[0]->token_to_string();
    auto v = env->get_value(name).get<List>();
    
    if(ast->nodes[1]->tag == "list_splice"_) { // 
        auto& iNodes = ast->nodes[1]->nodes;
        long l = -1;
        long r = -1;
        for(auto k : iNodes) {
            if(k->tag == "leftSp"_)
                l = eval(k, env).get<long>();
            else if(k->tag == "rightSp"_)
                r = eval(k, env).get<long>();
        }
        if(l != -1 && r == -1) { // list[x:]
            r = v.size();
        } else if(l == -1 && r != -1) { // list[:x]
            l = 0;
        } else if(l != -1 && r != -1) { // list[x:x]
            // continue.
        } else { // list[:]
            l = 0;
            r = v.size();
        }  
        auto fromList = eval(ast->nodes[2], env).get<List>();
        for(int i = l, j = 0; i < r; i ++, j++) {
            v[i] = fromList[j];
        }
    }
    else { // normal index assign
        int upper = 0;
        for(int i = 0; i < v.size(); i ++)
            if(v[i].v.index() != 0) upper++;
        auto index = eval(ast->nodes[1], env).get<long>();
        if(index < 0 || index >= upper) throw std::runtime_error("IndexError: list assignment index out of range");

        v[index] = eval(ast->nodes[2], env);
    }
    env->set_value(name, Value(v));

    return Value();
}

Value eval_while(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    *traceLog << "---- starting while loop" << std::endl;
    auto ifNode = ast->nodes[0]->nodes;
    auto oper = peg::str2tag(ifNode[1]->token_to_string());

    auto block = ast->nodes[1];

    bool done = false;
    unsigned int loopct = 0;

    while(done == false) 
    {
        long lhs = eval(ifNode[0], env).get<long>();
        long rhs = eval(ifNode[2], env).get<long>();
        *traceLog << "loop " << loopct << std::endl;
        switch(oper) 
        {
            case "=="_: 
                if(lhs == rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            case "<"_:
                if(lhs < rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            case "<="_:
                if(lhs <= rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            case ">"_:
                if(lhs > rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            case ">="_:
                if(lhs >= rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            case "!="_: 
                if(lhs != rhs) {
                    eval(block, env);
                } else {
                    done = true;
                }
                break;
            default:
                std::runtime_error("Invalid if comparator");
                break;
        }
        loopct++;
    }
    *traceLog << "---- end while loop" << std::endl;
    return Value();
}

Value eval_if(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    auto& ifNode = ast->nodes[0]->nodes;

    // evaluate condition
    long lhs = eval(ifNode[0], env).get<long>();
    auto oper = peg::str2tag(ifNode[1]->token_to_string());
    long rhs = eval(ifNode[2], env).get<long>();


    switch(oper) {
        case "=="_: 
            if(lhs == rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        case "<"_:
            if(lhs < rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        case "<="_:
            if(lhs <= rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        case ">"_:
            if(lhs > rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        case ">="_:
            if(lhs >= rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        case "!="_:
            if(lhs != rhs) {
                return eval(ast->nodes[1], env);
            } else if(ast->nodes.size() > 2) {
                return eval(ast->nodes[2], env);
            }
            break;
        default:
            std::runtime_error("Invalid if comparator");
    }
    return Value();
}

Value eval(const shared_ptr<Ast>& ast, const shared_ptr<Env>& env) {
    *traceLog << ast->name << std::endl;

    // Switch to eval proper token type
    switch (ast->tag) {


        case "program"_:  case "block"_:
            return eval_block(ast, env);

            
        case "expression"_:
            return eval_expr(ast, env);
        case "term"_:
            return eval_term(ast, env);


        case "NAME"_:
            return env->get_value(ast->token_to_string());
        case "STRING"_:
            return Value(ast->token_to_string());
        case "NUMBER"_:
            return Value(ast->token_to_number<long>());


        case "function"_:
            return declare_function(ast, env);
        case "call"_:
            return eval_call(ast, env);


        case "assignment"_: 
            return eval_assign(ast, env);
        case "list_assign"_:
            return list_assign(ast, env);



        case "list_create"_:
            return declare_list(ast, env);
        case "list_value"_:
            return access_list(ast, env);
        // case "list_splice"_:
        //     return splice_list(ast, env);

        case "if"_:
            return eval_if(ast, env);

        case "while"_:
            return eval_while(ast, env);

        default:
            if(ast->nodes.size()) return eval(ast->nodes[0], env);
    }
    return Value();
}

void interpret(shared_ptr<Ast> ast, std::ostream& os, std::ostream& trace, std::ostream& var, std::ostream& error) {
    auto global = std::make_shared<Env>();
    traceLog = &trace;
    varLog = &var;
    errorLog = &error;

    // Setup print function manually.
    global->set_value("print", Value(Function([&](const List& values) {
        *traceLog << "print called" << std::endl; 
        int count = 0;
        for(auto& v : values) {
            if(count++ > 0)
                os << " " << v.str();
            else
                os << v.str();
        }
        os << std::endl;
        return Value();
    })));

    // Setup len function. 
    global->set_value("len", Value(Function([&](const List& values) {
        assert(values.size() == 1); // make sure only 1 argument.
        return Value((long) values.back().get<List>().size()); // expected argument is list. so just check the container value otherwise typeerror is thrown automatically.
    })));
    eval(ast, global);
}