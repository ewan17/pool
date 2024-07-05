#!/bin/bash

# comment out the memcheck and uncomment the helgrind for synchronization
# valgrind='valgrind --tool=helgrind'
valgrind='valgrind --tool=memcheck --leak-check=yes --show-leak-kinds=all'

logfile="test.log"

executable="$1"

run_valgrind() {
    echo "Running Valgrind on: $executable"
    $valgrind ./$executable 2>&1 | tee $logfile
}

check_valgrind_output() {
    if grep -q "ERROR SUMMARY: 0 errors" $logfile; then
        echo "Valgrind tests passed."
        return 0
    else
        echo "Valgrind found errors:"
        return 1
    fi
}

if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    run_valgrind "$1"
    check_valgrind_output
else
    ./"$executable"
fi
RESULT=$?

if [ $RESULT -eq 0 ]; then
    echo "Overall: PASS"
    rm -f $logfile
else
    echo "Overall: FAIL"
fi

exit $RESULT