# Building my own shell

This week's learning goals were about understanding processes, signals,
and threads. So what better project to choose than building your own
small shell? Before I started, I had no real idea how a shell works
under the hood. In reality, it is surprisingly simple, and it really
opened my eyes to what bash or zsh are doing behind the scenes. Let's
jump right in.

## Basis for a shell

The basis for a shell are processes. Everything in C runs inside the
main process of the program. Only when you split off a new process do
you start introducing additional subprocesses. Processes follow a strict
hierarchy: there is the main process (the one that is running the
program), and if you `fork` for the first time you create a child
process of that main process. If you `fork` again from that child you
create another child --- this time a child of the child. In contrast to
threads, processes do **not** share memory by default. Every time a new
process is created, the OS creates a completely new address space with
independent variables, stack, and heap. Sharing data between processes
is therefore tricky.

One way to do it is inter-process communication. Unix offers multiple
mechanisms for this, and the one we explore here are pipes. Before that,
though, let's understand how to create a new process and then execute a
command inside it. We'll first look at how to fork, then how to execute
other programs from the child process, then how to handle signals like
SIGINT when a user presses CTRL+C. Finally, we'll see how to chain
processes together using pipes.

### Creating a new process

Let's imagine we are in our C program and want to start another process.
For that we need to `fork`:

``` c
pid_t pid = fork();
if (pid == 0) {
    // we're in the child process and can execute logic here
    printf("child pid: %d, parent pid: %d\n", getpid(), getppid());
} else if (pid > 0) {
    // Parent process, we wait for completion
    waitpid(pid, NULL, 0);
} else {
    perror("fork failure");
    exit(1);
}
```

With that we have successfully forked the program. Both processes
continue running. The parent process jumps into the `else if` block and
the child process into the `if` block. For me this was very unintuitive
at the beginning.

### Executing another command

After forking, we can implement arbitrary logic in the parent and child.
Since we want to build a shell, we also want to be able to execute
external commands like `ls` or `grep` from our main process. For that we
use the `exec` family of functions. Specifically we use `execvp`. This
function allows us to execute a variable-length **v**ector array while
searching the **p**ath for the executable that we provide.

``` c
pid_t pid = fork();
char *args[2] = {"ls", "-al", NULL};
if (pid == 0) {
    // child process
    printf("child pid: %d, parent pid: %d\n", getpid(), getppid());
    execvp(args[0], args);
    perror("execvp");
    _exit(1);
} else if (pid > 0) {
    waitpid(pid, NULL, 0);
} else {
    perror("fork failure");
    exit(1);
}
```

This will execute `ls` in the child process.

### Implementing our first mini shell

With that we now have all the tools to build a naive mini shell. We need
to provide the user with a way to specify input and then continuously
loop so our program doesn't immediately exit.

``` c
char *user_input = NULL;
size_t len = 0;

while (1) {
    printf("my-shell#Type>");
    int res = getline(&user_input, &len, stdin);
    if (res == -1) {
        printf("error reading line\n");
        break;
    }

    // Remove trailing newline (getline keeps it)
    user_input[strcspn(user_input, "\n")] = '\0';

    char *args[64];
    int i = 0;

    char *token = strtok(user_input, " ");
    while (token != NULL && i < 63) {
        args[i++] = token;
        token = strtok(NULL, " ");
    }
    args[i] = NULL;

    if (args[0] == NULL) {
        continue;
    }

    pid_t pid = fork();

    if (pid == 0) {
        execvp(args[0], args);
        perror("execvp");
        _exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork failure");
        exit(1);
    }
}
```

### Signals

The mini shell works, but what if the user hits CTRL+C? In a real shell
this interrupts only the child process, not the shell itself. To achieve
this we need to handle the `SIGINT` signal, which represents an
interrupt from the keyboard.

``` c
pid_t child_pid = 0;

void handle_sigint(int sig) {
    if (child_pid > 0) {
        // We're in the parent process
        kill(child_pid, SIGINT);
    }
}

signal(SIGINT, handle_sigint);
```

We also need to handle the termination of child processes. If we don't,
they become zombies:

``` c
void handle_sigchld(int sig) {
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0) {}
}

signal(SIGCHLD, handle_sigchld);
```

### Pipes

Now to pipes, which let processes communicate. Processes don't share
memory, so we need another way for them to send data around. A pipe
offers exactly that: one process writes data into one end, another reads
data from the other end.

``` c
int pipefd[2];

if (pipe(pipefd) == -1) {
    perror("pipe");
    exit(1);
}

int read_end = pipefd[0];
int write_end = pipefd[1];
```

One process writes, the other reads:

``` c
pid_t pid = fork();

if (pid == 0) {
    close(pipefd[0]);
    char buf[6] = {'H', 'E', 'L', 'L', 'O', '\0'};
    write(pipefd[1], buf, sizeof(buf));
    close(pipefd[1]);
} else if (pid > 0) {
    close(pipefd[1]);
    char buf[24];
    ssize_t amount_read = read(pipefd[0], buf, sizeof(buf));
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
} else {
    perror("fork");
    exit(1);
}
```

## Bringing it all together

Now we can combine everything into a fully functioning shell that
supports pipes and also allows us to change directories.

Before looking at the code, let's see conceptually how:

```shell
ls -al | grep my-shell | grep .c
```

works. We have three processes and two pipes. Process 1 doesn't read
from anything; it just writes to pipe 1. Process 2 reads from pipe 1 and
writes to pipe 2. Process 3 only reads from pipe 2 and writes to stdout.

<figure id="figure-1">
  <img src="diagrams/pipe.svg" alt="Basic piping">
  <figcaption><em>Figure 1: Communicating with pipes</em></figcaption>
</figure>
<br><br>

Let's set up the main loop: we install our signal handlers, read user input, parse it, and then execute whatever the
user asked for. I won't go into the details of `parse_user_input` here, but you can check it out in the codebase.

``` c
int main() {
	char *user_input = NULL;
	size_t len = 0;

	signal(SIGCHLD, handle_sigchld);
	signal(SIGINT, handle_sigint);

	while (1) {
		printf("my-shell#Type>");
		int res = getline(&user_input, &len, stdin);
		if (res == -1) {
			printf("error reading line\n");
			break;
		}

		int cmds_cap = 4;
		char ***cmds = malloc(sizeof(char **) * cmds_cap);
		int num_pipes = parse_user_input(cmds, cmds_cap, user_input);

		execute_pipeline(cmds, num_pipes);
		free(cmds);
	}
}
```

After parsing the input, the next step is executing the pipeline. For this we use `dup2`, which duplicates a file
descriptor and lets us redirect the standard input and output of a process. Since a user can chain multiple commands
together, we create an array of pipes (`pipefds`), one pipe per `|` operator. For each command in the pipeline we fork.
In
the child, if we're not the first command we redirect the previous pipe’s read end to STDIN_FILENO. If we're not the
last command we redirect the current pipe’s write end to STDOUT_FILENO. After setting up the redirections we close all
pipe ends and execvp the command.
the write end of our pipe.

```c
int execute_pipeline(char ***cmds, int num_pipes) {
	
	if (cmds[0] != NULL && strcmp(cmds[0][0], "cd") == 0)  {
		const char *dir = cmds[0][1];
		if (dir == NULL) {
		    dir = getenv("HOME");
		}

		if (chdir(dir) != 0) {
		    perror("cd");
		}
		return 0;

	}

	pid_t *pids = NULL;
	pids = malloc(sizeof(pid_t) * (num_pipes + 1));
	if (!pids) { perror("malloc"); exit(1); }
	
	int (*pipefds)[2] = NULL;
	if (num_pipes > 0) {
		pipefds = malloc(sizeof(int[2]) * num_pipes);
		if (!pipefds) { perror("malloc"); exit(1); }
	}

	for (int i = 0; i < num_pipes; i++) {
		if (pipe(pipefds[i]) == -1) {
			perror("pipe");
			exit(1);
		}
	}
	for (int i = 0; i < num_pipes + 1; i++) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork failure");
			exit(1);
		}
		if (pid == 0) {
			// child process
			if (i > 0) {
				dup2(pipefds[i-1][0], STDIN_FILENO);
			}

			if (i < num_pipes) {
				dup2(pipefds[i][1], STDOUT_FILENO);
			}

			for (int j = 0; j < num_pipes; j++) {
				close(pipefds[j][0]);
				close(pipefds[j][1]);
			}

			execvp(cmds[i][0], cmds[i]);
			perror("execvp");
			_exit(1);
		}
		
		if (i > 0) {
			close(pipefds[i-1][0]);
			close(pipefds[i-1][1]);
		}
		pids[i] = pid;

	}
	for (int i = 0; i < num_pipes; i++) {
		close(pipefds[i][0]);
		close(pipefds[i][1]);
	}

	for (int i = 0; i <= num_pipes; i++) {
		waitpid(pids[i], NULL, 0);
	
	}

	free(pids);
	free(pipefds);
	return 1;
}
```

## Running it locally

You can try the shell yourself by running:

```shell
make
./my-shell.out

# You will be greeted with:
my-shell#Type>
```

From here on you can use `my-shell` like any other shell.

## Caveats

This is still a very small shell. It supports pipes and handles signals, but it doesn’t implement redirection (`<`, `>`),
history, job control, or fancy formatting. Still, for a learning project this is the perfect scope — it teaches the
fundamentals of processes, signals, and pipes without getting lost in the weeds.

