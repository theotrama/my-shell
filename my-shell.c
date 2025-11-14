#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>


#define INITIAL_SIZE 2

pid_t child_pid = 0;

void handle_sigint(int sig) {
	if (child_pid > 0) {
		// Now we know we're in the parent process
		kill(child_pid, SIGINT);
	} 
}

void handle_sigchld(int sig) {
	pid_t pid;
	int status;
       
	while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
	}
}

int parse_user_input(char ***cmds, int cmds_cap, char *user_input) {

	user_input[strcspn(user_input, "\n")] = 0;
	int args_cap = 8;

	char **args = malloc(sizeof(char *) * args_cap);
	char *tok = strtok(user_input, " ");
	args[0] = tok;
	int count = 1;
	int outerCount = 0;
	while ((tok = strtok(NULL, " ")) != NULL) {
		if (*tok == '|') {
			args[count] = NULL;

			if (outerCount >= cmds_cap) {
				cmds_cap += 4;
				char ***tmp = realloc(cmds, sizeof(char**) *cmds_cap);			
				cmds = tmp;
			}

			cmds[outerCount] = args;
			outerCount++;

			args_cap = 8;
			args = malloc(sizeof(char *) * args_cap);
			count = 0;
			continue;
		}
		args_cap += 2;
		char **tmp = realloc(args, sizeof(char*) * (args_cap));
		if (tmp == NULL) {
			perror("error reallocating");
		}
		args = tmp;
		args[count] = tok;
		count++;
	}
	args[count] = NULL;
	cmds[outerCount++] = args;
	cmds[outerCount] = NULL;

	int num_pipes = (outerCount > 1) ? outerCount - 1 : 0;
	return num_pipes;
}

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
