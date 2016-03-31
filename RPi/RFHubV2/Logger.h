#include <iostream>
#include <sstream>

class LogLine {
public:
    LogLine(std::ostream& out = std::cout) : m_Out(out) {}
    ~LogLine() {
        m_Stream << "\n";
        m_Out << m_Stream.rdbuf();
        m_Out.flush();
    }
    template <class T>
    LogLine& operator<<(const T& thing) { m_Stream << thing; return *this; }
private:
    std::stringstream m_Stream;
    std::ostream& m_Out;
    //static LogFilter...
};