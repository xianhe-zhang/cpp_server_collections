#include <string>
#include <streambuf>

class LogStream : public std::streambuf {    
private:
    std::string buffer;
     
protected:
    int overflow(int ch) override {
        buffer.push_back((char) ch);
        if (ch == '\n') {
            // End of line, write to logging output and clear buffer.
             
            buffer.clear();
        }
         
        return ch;
         
        //  Return traits::eof() for failure.
    }
};