/*
 * Copyright (C) 2005 Patrick Ohly
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <cppunit/CompilerOutputter.h>
#include <cppunit/extensions/TestFactoryRegistry.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/TestListener.h>
#include <cppunit/TestResult.h>

#include <posix/base/posixlog.h>

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
    void startTest (CppUnit::Test *test) {
        currentTest = test->getName();
        setLogFile( "-", 0 );
        cerr << currentTest;
        string logfile = currentTest + ".log";
        simplifyFilename(logfile);
        remove(logfile.c_str());
        setLogFile( logfile.c_str(), 1 );
        failed = false;
    }
    void addFailure(const CppUnit::TestFailure &failure) {
        failed = true;
    }
    void endTest (CppUnit::Test *test) {
        setLogFile( "-", 0 );
        if (failed) {
            cerr << " *** failed ***";
        }
        cerr << "\n";
    }
    string currentTest;
    bool failed;
} syncListener;

const string &getCurrentTest() {
    return syncListener.currentTest;
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

  // Our tests need to know the currently running test,
  // register a TestListener to remember that information.
  // If there is a better way to obtain that information
  // I did not find it in the CppUnit documentation...
  runner.eventManager().addListener(&syncListener);

  try {
      // Run the tests.
      bool wasSucessful = runner.run(argc > 1 ? argv[1] : "", false, true, false);

      // Return error code 1 if the one of test failed.
      return wasSucessful ? 0 : 1;
  } catch (invalid_argument e) {
      // Test path not resolved
      std::cerr << std::endl  
                << "ERROR: " << e.what()
                << std::endl;
      return 1;
  }
}
