#include <string>
#include <stack>

using std::stack;
using std::string;

string pythonCFL(string src) {
    stack<int> levels;
    levels.push(0);
    int n = 0;
    string output = "";
    for(int i = 0; i < src.size(); i++) 
    {
        if(src[i] == '\n') 
        {
            output += '\n';
            if(i < src.size() && src[i+1] != '\n') {
                n = 0;
                while(i < src.size()-1 && src[i+1] == ' ') 
                {
                    n++;       
                    i++;
                }
                if(n > levels.top()) 
                {
                    output += "{\n";
                    levels.push(n);
                }

                while(n < levels.top()) 
                {
                    output += "\n}";
                    levels.pop();
                    if(levels.top() < n)
                        std::runtime_error("Invalid indentation");
                }
            }
        } 

        if(src[i] != '\n')
            output += src[i];
    }
    while(levels.top() != 0) {
        output += "\n}";
        levels.pop();
    }
    return output;
}