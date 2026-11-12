#include <assert.h>
#include <stdio.h>
#include <kernel/task_wait.h>
int main(void) {
    assert(task_wait_selector_valid(-1));
    assert(task_wait_selector_valid(1));
    assert(!task_wait_selector_valid(0));
    assert(!task_wait_selector_valid(-2));
    assert(task_wait_child_matches(10,10,22,22));
    assert(task_wait_child_matches(10,10,22,-1));
    assert(!task_wait_child_matches(11,10,22,22));
    assert(!task_wait_child_matches(10,11,22,-1));
    assert(!task_wait_child_matches(10,10,23,22));
    assert(task_wait_nohang_should_return(true,true,false));
    assert(!task_wait_nohang_should_return(false,true,false));
    assert(!task_wait_nohang_should_return(true,false,false));
    puts("PASS: waitpid selector, direct-child ownership and WNOHANG policy");
    return 0;
}
