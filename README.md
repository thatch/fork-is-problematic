# Fork is Problematic

Recall from Operating Systems a high-level overview of what `fork(2)` does:

1. It returns twice, once in the original (parent) process, and once in a new (child) process.
2. You can use the return value to distinguish in which copy your code is continuing.
3. "Everything" gets copied from the parent to the child (these days, using CoW).
4. A very typical use of `fork(2)` is to then use `execve(2)` or similar.

## "Everything"?

Among the things that get copied are memory maps (so that pointers remain
valid), which includes globals and mutexes by virtue of them being stored in
memory.

The file descriptor table is also copied, although with the explicit note that
the fd copies (unlike memory) do not maintain distinct state (e.g. position:
a seek in the parent post-fork will inadvertently seek in the child).

Only one thread (the one that calls `fork(2)`) will remain, all others "disappear."

## Why?

Basically, because this is the way some old Unix vendors implemented this in
the 70's.  It got codified into POSIX and Linux (as well as OS X) obey that.

If you'd like to follow along, open a Terminal and run `make` in the root of
this repo.

## Problem 1: Global state

Quite often, I/O is buffered.  What this means is that your call to `printf(3)`
doesn't have to output immediately -- it can write to an internal memory
buffer, and only output when that is "full" or when the program is "exiting".

Look at the example `1.c` which prints something and exits.  This outputs the
same, regardless of whether it's being piped.

```sh
$ ./1
hi!
$ ./1 | cat
hi!
```

The reason for this is that there are hooks that get called when using
`exit(3)` and among those is to flush buffers.  If you use the syscall
`_exit(2)` as `1b.c` does, then the output differs.

```sh
$ ./1b
hi!
$ ./1b | cat
```

Notably, signals also interrupt normal exit, as in `1c.c`.  The output is a
little more verbose because the shell knows it got killed, but the important
part is that `hi!` is not printed in the second case.

```sh
$ ./1c
hi!
zsh: terminated  ./1c
$ ./1c | cat
zsh: terminated  ./1c | 
zsh: done        cat
```

## Problem 2: State is copied

In addition to there *being* a buffer, that buffer is necessarily in memory.
Unless some hook is called, that (potentially incomplete) state is copied to
the child in a way that might not be ~idempotent.

```sh
$ ./2
parent 58868
child 58868
parent 58868
child 58869
```

You probably expected the `parent` line to only output once.  However, that
not-yet-output data was copied to the child, and it didn't know any better than
to output it (in both the parent, and the child) when exiting.

The way around this is once again hooks; if you call `flush(3)` then the state
is "reset" and not carried to the child, and it works "correctly."  I've used
syscalls (man section 2) here to show what `libc` is basically doing.

```sh
$ ./2b
parent 59022
child 59022
child 59023
```

While `libc` does have a concept of `atexit` hooks, it does not have one of
`atfork` -- Python does when you call `os.fork` but these don't get run if a C
library calls `fork(2)` directly (just as `atexit` hooks don't when you bypass
the library function and use `_exit(2)`).

A common workaround is to store the expected `pid` in a global variable, and
call `getpid()` periodically to recognize if you've forked.  This is expensive,
and not actually a "fix" -- just "detection."  You can't know whether the
parent will ever/timely flush its buffer to just reset in the child, for example.

OTEL uses both of these in the [python implementation](https://github.com/open-telemetry/opentelemetry-python/blob/679297f5ebd37510b6c9e086fc27837935d57e81/opentelemetry-sdk/src/opentelemetry/sdk/trace/export/__init__.py#L202-L204)
-- it assumes that spans queued pre-fork will be handled by the parent (not
guaranteed, it might get killed or interrupted by a signal), but the child
starts fresh.

## Problem 3: Locks are copied, threads are ~not

Only the thread that calls `fork(2)` remains in the child, all others disappear
(with no hook).  First, demonstrating that the child doesn't inherit them, we
should get 7 lines (not 11, if they were) from running this:

```
$ ./3
60144 0 t1
a
60144 0 t2
60144 1 t1
60144 1 t2
b 60145
b 0
```

However, a bunch of the time (nondeterminstically) this hangs.  If you attach a
debugger, you'll see it's waiting for a lock -- it was locked in the parent and
because there aren't `atfork` hooks, that lock remains locked in the child.
Trust me that one of the threads was outputting (thus, held the lock) when the
fork happened if it hangs.

Thus, the only safe state for locks during a fork is either unlocked, or locked
(by an atfork hook directly).  But you can't just free all locks when you fork,
and neither can you realistically acquire "all" locks, fork, then release them
even if you had a hook to let you run that code.

If you use buffered I/O, the time spent under the lock is less (so the
likelihood of deadlock is less), but it still outputs duplicate entries just as
in problem 2.

```sh
$ ./3 | cat
a
60501 0 t2
60501 0 t1
b 0
a
60501 0 t2
60501 0 t1
60501 1 t2
60501 1 t1
b 60503
```

To avoid deadlock, many libraries include [workarounds that reinitialize locks](https://github.com/open-telemetry/opentelemetry-python/blob/679297f5ebd37510b6c9e086fc27837935d57e81/opentelemetry-sdk/src/opentelemetry/sdk/trace/export/__init__.py#L231-L241)
in the child regardless of their prior state.  As in the previous OTEL example,
this trusts that the parent should be responsible for any state that happened
before the fork.  This leads to error-prone code, for example OTEL also [checks the pid](https://github.com/open-telemetry/opentelemetry-python/blob/679297f5ebd37510b6c9e086fc27837935d57e81/opentelemetry-sdk/src/opentelemetry/sdk/trace/export/__init__.py#L217-L218)
but that internally uses a lock that doesn't get reinitialized, which I think
means this doesn't always work and can
deadlock if that [lock is held](https://github.com/open-telemetry/opentelemetry-python/blob/679297f5ebd37510b6c9e086fc27837935d57e81/opentelemetry-api/src/opentelemetry/util/_once.py#L42-L46) when double-forking and there's another thread in the child.

What I'm trying to get across is that these are hacks -- libraries certainly do
them, because they benefit users -- but not proper solutions.  Hang on, it gets
worse.

## Problem 4: File descriptors are shared

This example is orthogonal to the lock issues -- file descriptors share state
across forked processes.  What this means is that reads can interleave.

You might get lucky -- here the two processes read this in some reasonable
order, and if you can imagine this being a pipelined request-response, maybe
the second one thought it was going to see 123 but saw the equally valid 456
and misassociated a response with a request.  You might not even notice this in
your application code, depending on what the library does with such things.

```sh
$ echo 123456 | ./4
123
456
```

However, you might not be lucky.  Each process still does a read and gets a
byte in the correct order, but because the pointer is unexpectedly advanced,
sees a confusing set of data.

```sh
$ echo 123456 | ./4
124
356
```

If we were trying to parse a proto, or use encryption, this would likely result
in some confusing low-level errors.  This applies to files as well as sockets.

As long as you're reading a given fd only in the parent, or only in the child,
the fork actually isn't that bad for this case.  The problem arises when you do
both.

## Problem 5: GRPC stores its set of open fd's in memory

I haven't written up a C example for this, but suffice it to say that many
network libraries store a set of open connections, and use `select(2)` on them.
This gets worse with connection pooling, because you might keep one around that
could get a (different) second request sent from two different children, with
different encryption state (resulting in confusing errors, rather than just
mismatched replies).

## Problem 6: Why isn't this a problem in Java?

Because they don't use fork (as much).  There might be some JNI library that
forks, but that would result in the same "all bets are off" problems we talk
about here, so people don't do it.

Python libraries do use multiprocessing to get CPU-bound workloads to
parallelize -- they wouldn't otherwise because of the GIL.  That said, there
are actually three backends that multiprocessing can use, in decreasing order
of priority and well-definedness:

1. `spawn` which uses the `posix_spawn(2)` syscall which does a fork+exec and
carries basically no state from the parent.  As an implementation detail, the
command line is customizable, and file desciptors can be deliberately
inherited, which is how Python communicates with a child over a pickle-based
protocol.
2. `forkserver` when started early enough first forks a boring enough child
that global state (like using grpc in the parent) hasn't happened yet.  When
the parent needs another child, it talks to the `forkserver` process which
creates a grandchild that inherits the boring state.  As mentioned above, using
grpc in either the parent or the child is safe-ish, and this is a way you can
do both.
3. `fork` evokes all the problems mentioned here.

## Problem 7: Why isn't this documented?

It totally is, although the language might not be scary enough to make this obvious:

`fork(2)` on OS X says:

> The child process has its own copy of the parent's descriptors.  These
> descriptors reference the same underlying objects, so that, for instance,
> file pointers in file objects are shared between the child and the parent, so
> that an lseek(2) on a descriptor in the child process can affect a subsequent
> read or write by the parent.

At the very end it also says

> CAVEATS
>
> There are limits to what you can do in the child process.  To be totally safe
> you should restrict yourself to only executing async-signal safe operations
> until such time as one of the exec functions is called.  All APIs, including
> global data symbols, in any framework or library should be assumed to be
> unsafe after a fork() unless explicitly documented to be safe or async-signal
> safe.

The term "async-signal safe" is kind of jargony, but almost nothing is safe,
not even `printf(3)`.  The approved use of fork is to exec, which is what
clears state.  The modern `posix_spawn(2)` is just that without being a
footgun.

The Python [os.fork](https://docs.python.org/3/library/os.html#os.fork) docs also now state:

> We chose to surface [multiple threads existing when you call os.fork] as a
> warning, when detectable, to better inform developers of a design problem
> that the POSIX platform specifically notes as not supported. Even in code
> that appears to work, it has never been safe to mix threading with os.fork()
> on POSIX platforms. The CPython runtime itself has always made API calls that
> are not safe for use in the child process when threads existed in the parent
> (such as malloc and free).
>
> See [this discussion](https://discuss.python.org/t/concerns-regarding-deprecation-of-fork-with-alive-threads/33555)
> on fork being incompatible with threads for technical details of why we’re
> surfacing this longstanding platform compatibility problem to developers.

## Solutions

Parallelism is properly supported in Python when using threads (as long as
you're I/O-bound, not CPU-bound), or spawn, and in a pinch, forkserver.

Those represent two extremes of what gets shared -- in threads, you can
directly refer to objects that can't (easily) be recreated, while in spawn or
forkserver they need to all be pickleable.  Not all objects are pickleable, in
particular inner functions because they are closures.  There are generally ways
to rewrite these to use top-level functions along with explicit currying
(`functools.partial`) which can be pickled.

## Forward-looking Solutions

There is a somewhat experimental `nogil` Python interpreter which hopefully
will stabilize and be widely adopted starting in 2025 which allows you to get
multicore parallelism when using threads, making all of this fork/spawn
business unnecessary.  That's going to be some work for a lot of third-party
library authors in particular, but is a breath of clean air and the better way
forward.  As long as anyone forks, we can still have problems.
