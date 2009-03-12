/*
 * Copyright (C) 2005-2008 Patrick Ohly
 */

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/TestListener.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestFailure.h>
#include <cppunit/extensions/HelperMacros.h>

#include <posix/base/posixlog.h>

#include <unistd.h>
#include <signal.h>

#include <string>
#include <stdexcept>
using namespace std;

#include "Test.h"

class Sync4jOutputter : public CppUnit::CompilerOutputter {
public:
    Sync4jOutputter(CppUnit::TestResultCollector *result, std::ostream &stream) :
        CompilerOutputter(result, stream) {}
    void write() {
        // ensure that output goes to console again
        setLogFile("sync.log", 0);
        CompilerOutputter::write();
    }
};

class Sync4jListener : public CppUnit::TestListener {
public:
    Sync4jListener() :
        m_failed(false) {
        // install signal handler which turns an alarm signal into a runtime exception
        // to abort tests which run too long
        const char *alarm = getenv("TEST_EVOLUTION_ALARM");
        m_alarmSeconds = alarm ? atoi(alarm) : -1;

        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_handler = alarmTriggered;
        action.sa_flags = SA_NOMASK;
        sigaction(SIGALRM, &action, NULL);
    }

    void addAllowedFailures(string allowedFailures) {
        int start = 0, end;
        while ((end = allowedFailures.find(',', start)) != allowedFailures.npos) {
            int len = end - start;
            if (len) {
                m_allowedFailures.insert(allowedFailures.substr(start, len));
            }
            start = end + 1;
        }
        if (allowedFailures.size() > start) {
            m_allowedFailures.insert(allowedFailures.substr(start));
        }
    }

    void startTest (CppUnit::Test *test) {
        m_currentTest = test->getName();
        setLogFile( "-", 0 );
        cerr << m_currentTest;
        string logfile = m_currentTest + ".log";
        simplifyFilename(logfile);
        remove(logfile.c_str());
        setLogFile( logfile.c_str(), 1 );
        m_testFailed = false;

        if (m_alarmSeconds > 0) {
            alarm(m_alarmSeconds);
        }
    }

    void addFailure(const CppUnit::TestFailure &failure) {
        m_testFailed = true;
    }

    void endTest (CppUnit::Test *test) {
        if (m_alarmSeconds > 0) {
            alarm(0);
        }

        setLogFile( "-", 0 );
        if (m_testFailed) {
            if (m_allowedFailures.find(m_currentTest) == m_allowedFailures.end()) {
                cerr << " *** failed ***";
                m_failed = true;
            } else {
                cerr << " *** failure ignored ***";
            }
        }
        cerr << "\n";
    }

    bool hasFailed() { return m_failed; }
    const string &getCurrentTest() const { return m_currentTest; }

private:
    set<string> m_allowedFailures;
    bool m_failed, m_testFailed;
    string m_currentTest;
    int m_alarmSeconds;

    static void alarmTriggered(int signal) {
        CPPUNIT_ASSERT_MESSAGE(false, "test timed out");
    }
} syncListener;

const string &getCurrentTest() {
    return syncListener.getCurrentTest();
}

void simplifyFilename(string &filename)
{
    size_t pos = 0;
    while (true) {
        pos = filename.find(":", pos);
        if (pos == filename.npos ) {
            break;
        }
        filename.replace(pos, 1, "_");
    }
}

int main(int argc, char* argv[])
{
  // Get the top level suite from the registry
  CppUnit::Test *suite = CppUnit::TestFactoryRegistry::getRegistry().makeTest();

  // Adds the test to the list of test to run
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( suite );

  // Change the default outputter to a compiler error format outputter
  runner.setOutputter( new Sync4jOutputter( &runner.result(),
                                            std::cerr ) );

  // track current test and failure state
  const char *allowedFailures = getenv("TEST_EVOLUTION_FAILURES");
  if (allowedFailures) {
      syncListener.addAllowedFailures(allowedFailures);
  }
  runner.eventManager().addListener(&syncListener);

  try {
      // Run the tests.
      if (argc <= 1) {
          // all tests
          runner.run("", false, true, false);
      } else {
          // run selected tests individually
          for (int test = 1; test < argc; test++) {
              runner.run(argv[test], false, true, false);
          }
      }

      // Return error code 1 if the one of test failed.
      return syncListener.hasFailed() ? 1 : 0;
  } catch (invalid_argument e) {
      // Test path not resolved
      std::cerr << std::endl  
                << "ERROR: " << e.what()
                << std::endl;
      return 1;
  }
}
