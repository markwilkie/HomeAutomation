#include <iostream>
#include <sstream>
#include <string.h>
#include <time.h>
#include <sys/resource.h> 

class LogLine {
public:
    LogLine(std::ostream& out = std::cout) : m_Out(out) {}
    ~LogLine() {

        m_Out << m_Stream.rdbuf();

        getrusage(RUSAGE_SELF,&memInfo);
        time(&rawtime);
        timeinfo = localtime (&rawtime);
        char *time=asctime(timeinfo);
        time[(strlen(time))-1]=0;
        m_Out << "   -: " << memInfo.ru_maxrss << " " << time << "\n";

        m_Out.flush();
    }
    template <class T>
    LogLine& operator<<(const T& thing) { m_Stream << thing; return *this; }
private:
    std::stringstream m_Stream;
    std::ostream& m_Out;

    time_t rawtime;
    struct tm * timeinfo;
    struct rusage memInfo;
};

class LogErrorLine {
public:
    LogErrorLine (std::ostream& out = std::cerr) : m_Out(out) {}
    ~LogErrorLine () {

        //m_Stream << "\n";
        m_Out << m_Stream.rdbuf();

        getrusage(RUSAGE_SELF,&memInfo);
        time(&rawtime);
        timeinfo = localtime (&rawtime);
        char *time=asctime(timeinfo);
        time[(strlen(time))-1]=0;
        m_Out << "   -: " << memInfo.ru_maxrss << " " << time << "\n";

        m_Out.flush();
    }
    template <class T>
    LogErrorLine& operator<<(const T& thing) { m_Stream << thing; return *this; }
private:
    std::stringstream m_Stream;
    std::ostream& m_Out;

    time_t rawtime;
    struct tm * timeinfo;
    struct rusage memInfo;
};