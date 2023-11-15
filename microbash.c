/*
 * Micro-bash v2.2
 *
 * Programma sviluppato a supporto del laboratorio di Sistemi di
 * Elaborazione e Trasmissione dell'Informazione del corso di laurea
 * in Informatica presso l'Università degli Studi di Genova, a.a. 2022/2023.
 *
 * Copyright (C) 2020-2022 by Giovanni Lagorio <giovanni.lagorio@unige.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

void fatal(const char * const msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(EXIT_FAILURE);
}

void fatal_errno(const char * const msg)
{
	perror(msg);
	exit(EXIT_FAILURE);
}

void *my_malloc(size_t size)
{
	void *rv = malloc(size);
	if (!rv)
		fatal_errno("my_malloc");
	return rv;
}

void *my_realloc(void *ptr, size_t size)
{
	void *rv = realloc(ptr, size);
	if (!rv)
		fatal_errno("my_realloc");
	return rv;
}

char *my_strdup(char *ptr)
{
	char *rv = strdup(ptr);
	if (!rv)
		fatal_errno("my_strdup");
	return rv;
}

#define malloc I_really_should_not_be_using_a_bare_malloc
#define realloc I_really_should_not_be_using_a_bare_realloc
#define strdup I_really_should_not_be_using_a_bare_strdup

static const int NO_REDIR = -1;

typedef enum { CHECK_OK = 0, CHECK_FAILED = -1 } check_t;

static const char *const CD = "cd";

typedef struct {
	int n_args;
	char **args; // in an execv*-compatible format; i.e., args[n_args]=0
	char *out_pathname; // 0 if no output-redirection is present
	char *in_pathname; // 0 if no input-redirection is present
} command_t;

typedef struct {
	int n_commands;
	command_t **commands;
} line_t;

void free_command(command_t * const c)
{
	assert(c==0 || c->n_args==0 || (c->n_args > 0 && c->args[c->n_args] == 0)); /* sanity-check: if c is not null, then it is either empty (in case of parsing error) or its args are properly NULL-terminated */
	/*** TO BE DONE START ***/
	
	// Liberiamo prima la memoria di questi due puntatori
	free(c->out_pathname);
	free(c->in_pathname);
	
	// Essendo c->args un puntatore ad un array di puntatori, dobbiamo prima 
	// liberare la memoria dei puntatori nell'array...
	for(int i = c->n_args; i > 0; --i)
		free(c->args[i-1]);
	
	// e poi liberare il puntatore all'array di puntatori
	free(c->args);
	
	// Finalmente liberiamo il puntatore allo struct del comando
	free(c);
	
	/*** TO BE DONE END ***/
}

void free_line(line_t * const l)
{
	assert(l==0 || l->n_commands>=0); /* sanity-check */
	/*** TO BE DONE START ***/
	
	// Il concetto è simile al ciclo for nella "free_command"...
	for(int i = l->n_commands; i > 0; --i)
		free_command(l->commands[i-1]);
	
	free(l->commands);
	
	// Liberiamo la memoria del puntatore allo struct della linea
	free(l);
	
	/*** TO BE DONE END ***/
}

#ifdef DEBUG
void print_command(const command_t * const c)
{
	if (!c) {
		printf("Command == NULL\n");
		return;
	}
	printf("[ ");
	for(int a=0; a<c->n_args; ++a)
		printf("%s ", c->args[a]);
	assert(c->args[c->n_args] == 0);
	printf("] ");
	printf("in: %s out: %s\n", c->in_pathname, c->out_pathname);
}

void print_line(const line_t * const l)
{
	if (!l) {
		printf("Line == NULL\n");
		return;
	}
	printf("Line has %d command(s):\n", l->n_commands);
	for(int a=0; a<l->n_commands; ++a)
		print_command(l->commands[a]);
}
#endif

command_t *parse_cmd(char * const cmdstr)
{
	static const char *const BLANKS = " \t";
	command_t * const result = my_malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	char *saveptr, *tmp;
	tmp = strtok_r(cmdstr, BLANKS, &saveptr);
	while (tmp) {
		result->args = my_realloc(result->args, (result->n_args + 2)*sizeof(char *));
		if (*tmp=='<') {
			if (result->in_pathname) {
				fprintf(stderr, "Parsing error: cannot have more than one input redirection\n");
				goto fail;
			}
			if (!tmp[1]) {
				fprintf(stderr, "Parsing error: no path specified for input redirection\n");
				goto fail;
			}
			result->in_pathname = my_strdup(tmp+1);
		} else if (*tmp == '>') {
			if (result->out_pathname) {
				fprintf(stderr, "Parsing error: cannot have more than one output redirection\n");
				goto fail;
			}
			if (!tmp[1]) {
				fprintf(stderr, "Parsing error: no path specified for output redirection\n");
				goto fail;
			}
			result->out_pathname = my_strdup(tmp+1);
		} else {
			if (*tmp=='$') {
				/* Make tmp point to the value of the corresponding environment variable, if any, or the empty string otherwise */
				/*** TO BE DONE START ***/
				
				// Cerchiamo una variabile d'ambiente con getenv, se non lo trova 
				// facciamo puntare "tmp" ad una stringa vuota 
				if((tmp = getenv(tmp + sizeof(char))) == NULL)
					tmp = ""; 
				
				/*** TO BE DONE END ***/
			}
			result->args[result->n_args++] = my_strdup(tmp);
			result->args[result->n_args] = 0;
		}
		tmp = strtok_r(0, BLANKS, &saveptr);
	}
	if (result->n_args)
		return result;
	fprintf(stderr, "Parsing error: empty command\n");
fail:
	free_command(result);
	return 0;
}

line_t *parse_line(char * const line)
{
	static const char * const PIPE = "|";
	char *cmd, *saveptr;
	cmd = strtok_r(line, PIPE, &saveptr);
	if (!cmd)
		return 0;
	line_t *result = my_malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));
	while (cmd) {
		command_t * const c = parse_cmd(cmd);
		if (!c) {
			free_line(result);
			return 0;
		}
		result->commands = my_realloc(result->commands, (result->n_commands + 1)*sizeof(command_t *));
		result->commands[result->n_commands++] = c;
		cmd = strtok_r(0, PIPE, &saveptr);
	}
	return result;
}

check_t check_redirections(const line_t * const l)
{
	assert(l);
	/* This function must check that:
	 * - Only the first command of a line can have input-redirection
	 * - Only the last command of a line can have output-redirection
	 * and return CHECK_OK if everything is ok, print a proper error
	 * message and return CHECK_FAILED otherwise
	 */
	/*** TO BE DONE START ***/
	
	// Se c'è solo un comando, allora è valida sia la redirezione in input ed in output (vedere PDF)...
	if(l->n_commands == 1)
		return CHECK_OK;	
	
	// Si esegue una scansione sulla linea per cercare redirezioni, e verificare 
	// il regolamento definito dal PDF (redirezione-input solo nel primo comando, 
	// redirezione output solo nell'ultimo comando); se fallisce ritorna CHECK_FAILED
	for(int i = 0; i < l->n_commands; ++i){
		if(l->commands[i]->in_pathname)
			if(i != 0){
				printf("Input redirection must be only in first command\n");
				return CHECK_FAILED;
			}
		
		if(l->commands[i]->out_pathname)
			if(i != (l->n_commands-1)){
				printf("Output redirection must be only in last command\n");
				return CHECK_FAILED;
			}
	}
	
	/*** TO BE DONE END ***/
	return CHECK_OK;
}


check_t check_cd(const line_t * const l)
{
	assert(l);
	/* This function must check that if command "cd" is present in l, then such a command
	 * 1) must be the only command of the line
	 * 2) cannot have I/O redirections
	 * 3) must have only one argument
	 * and return CHECK_OK if everything is ok, print a proper error
	 * message and return CHECK_FAILED otherwise
	 */
	/*** TO BE DONE START ***/

	// Ricerca a tappeto per trovare un comando con "cd"
	for(int i = 0; i < l->n_commands; ++i){
		for(int j = 0; j < l->commands[i]->n_args; ++j){
			
			// Se trovo un comando con "cd" entro qui'
			if(!strncmp("cd", l->commands[i]->args[j], 2)){
				
				// Se la linea è composta da più comandi, ritorno errore
				if(l->n_commands != 1){
					printf("Error: cd must be used alone\n");
					return CHECK_FAILED;
				}
				
				// Se il comando ha redirezioni I/O, ritorno errore
				if(l->commands[i]->out_pathname != 0 || l->commands[i]->in_pathname != 0){
					printf("Error: cd cannot redirect in input or output\n");
					return CHECK_FAILED;
				}
				
				// Se il comando ha più di un argomento, ritorno errore
				if(l->commands[i]->n_args != 2){
					printf("Error: Command cd must have only one argument\n");
					return CHECK_FAILED;
				}
				
				// Se passa tutti i check, allora esco dal ciclo e ritorno true
				return CHECK_OK;
			}
		}
	}
	
	/*** TO BE DONE END ***/
	return CHECK_OK;
}


void wait_for_children()
{
	/* This function must wait for the termination of all child processes.
	 * If a child exits with an exit-status!=0, then you should print a proper message containing its PID and exit-status.
	 * Similarly, if a child is killed by a signal, then you should print a message specifying its PID, signal number and signal name.
	 */
	/*** TO BE DONE START ***/
	int status;
	pid_t pid = 0;
	
	// Finchè esistono dei figli attivi o zombie...
	while (pid != -1){
		pid = wait(&status);
		if (WIFSIGNALED(status) && pid != -1) { // Controllo se il child è stato chiuso da un Signal
				// WTERMSIG ritorna il valore del signal number che ha chiuso il processo child.
				// strsignal() ci permette di convertire il numero del segnale in una stringa corrispondente.
				char *signal_name = strsignal(WTERMSIG(status));
				printf("Process child %d closed by %s, signal number: %d\n", pid, signal_name, WTERMSIG(status));
		}
		else if (WIFEXITED(status) && pid != -1) {
			// WIFEXITED necessario per il controllo di corretta chiusura del processo child, 
			// dato che WEXITSTATUS ritorna sempre in ogni caso.
			int exit_status = WEXITSTATUS(status);
			if(exit_status != 0)
				printf("Process with ID %d terminated with status: %d\n", pid, exit_status);
		}
	}
	
	/*** TO BE DONE END ***/
}

void redirect(int from_fd, int to_fd)
{
	/* If from_fd!=NO_REDIR, then the corresponding open file should be "moved" to to_fd.
	 * That is, use dup/dup2/close to make to_fd equivalent to the original from_fd, and then close from_fd
	 */
	/*** TO BE DONE START ***/
	
	if (from_fd != NO_REDIR) {
		if (dup2(from_fd, to_fd) == -1) fatal_errno("dup2 error in redirect"); //dup2 fa un copia del from_fd su to_fd
		else if (close(from_fd) == -1) fatal_errno("close error in redirect");
	}
	
	/*** TO BE DONE END ***/
}

void run_child(const command_t * const c, int c_stdin, int c_stdout)
{
	/* This function must:
	 * 1) create a child process, then, in the child
	 * 2) redirect c_stdin to STDIN_FILENO (=0)
	 * 3) redirect c_stdout to STDOUT_FILENO (=1)
	 * 4) execute the command specified in c->args[0] with the corresponding arguments c->args
	 * (printing error messages in case of failure, obviously)
	 */
	/*** TO BE DONE START ***/
	
	// Si crea il figlio
	pid_t p=fork();
	
	// Si controlla se è stato creato correttamente
	if(p==-1)fatal_errno("fork:");

	// Si avvia il processo figlio
	if(p==0){
		redirect(c_stdin,STDIN_FILENO);
		redirect(c_stdout,STDOUT_FILENO);
		if(execvp(c->args[0],c->args) ==-1) fatal_errno("Exec error");
	}
	
	/*** TO BE DONE END ***/
}

void change_current_directory(char *newdir)
{
	/* Change the current working directory to newdir
	 * (printing an appropriate error message if the syscall fails)
	 */
	/*** TO BE DONE START ***/
	
	// Si cambia la directory attuale, se non esiste si rimane nella cartella 
	// corrente e si mostra un messaggio di errore
	if(chdir(newdir) == -1)
		printf("cd: No such file or directory\n");
	
	/*** TO BE DONE END ***/
}

void close_if_needed(int fd)
{
	if (fd==NO_REDIR)
		return; // nothing to do
	if (close(fd))
		perror("close in close_if_needed");
}

void execute_line(const line_t * const l)
{
	if (strcmp(CD, l->commands[0]->args[0])==0) {
		assert(l->n_commands == 1 && l->commands[0]->n_args == 2);
		change_current_directory(l->commands[0]->args[1]);
		return;
	}
	int next_stdin = NO_REDIR;
	for(int a=0; a<l->n_commands; ++a) {
		int curr_stdin = next_stdin, curr_stdout = NO_REDIR;
		const command_t * const c = l->commands[a];
		if (c->in_pathname) {
			assert(a == 0);
			/* Open c->in_pathname and assign the file-descriptor to curr_stdin
			 * (handling error cases) */
			/*** TO BE DONE START ***/
			
			// Si preparano i flag ed il file descriptor per la redirezione in input
			if((curr_stdin = open(c->in_pathname, O_RDONLY)) == -1){
				if(errno == EACCES || errno == ENOENT) perror("error");
				else fatal_errno("Failed input-redirection");
			}
			
			/*** TO BE DONE END ***/
		}
		if (c->out_pathname) {
			assert(a == (l->n_commands-1));
			/* Open c->out_pathname and assign the file-descriptor to curr_stdout
			 * (handling error cases) */
			/*** TO BE DONE START ***/
			
			// Si preparano il file descriptor ed i flag per la redirezione in output
			// (nel caso il file non esista allora si crea)
			if((curr_stdout = open(c->out_pathname, O_RDWR | O_CREAT, 00666)) == -1){
				if(errno == EACCES) perror("error");
				else fatal_errno("Failed output-redirection");
			}
			
			/*** TO BE DONE END ***/
		} else if (a != (l->n_commands - 1)) { /* unless we're processing the last command, we need to connect the current command and the next one with a pipe */
			int fds[2];
			/* Create a pipe in fds, and set FD_CLOEXEC in both file-descriptor flags */
			/*** TO BE DONE START ***/
			
			// Si inizializza la pipe...
			if(pipe(fds) == -1)
				fatal_errno("Pipe creation error");
			
			// Usiamo flags per salvare i precedenti flag (se esistono) della pipe "fds[0]"
			int flags;
			if((flags = fcntl(fds[0], F_GETFD)) == -1) fatal_errno("fcntl1 error");
			// Applichiamo alla pipe [0] i flag precedenti + FD_CLOEXEC (chiudi il file 
			// descriptor alla fine dell'operazione)
			if(fcntl(fds[0], F_SETFD, FD_CLOEXEC | flags) == -1)
				fatal_errno("fcntl on curr_stdin error");
			
			// Discorso uguale per fds[1]
			if((flags = fcntl(fds[1], F_GETFD)) == -1) fatal_errno("fcntl2 error");
			if(fcntl(fds[1], F_SETFD, FD_CLOEXEC | flags) == -1)
				fatal_errno("fcntl on curr_stdout error");
			
			/*** TO BE DONE END ***/
			curr_stdout = fds[1];
			next_stdin = fds[0];
		}
		run_child(c, curr_stdin, curr_stdout);
		close_if_needed(curr_stdin);
		close_if_needed(curr_stdout);
	}
	wait_for_children();
}

void execute(char * const line)
{
	line_t * const l = parse_line(line);
#ifdef DEBUG
	print_line(l);
#endif
	if (l) {
		if (check_redirections(l)==CHECK_OK && check_cd(l)==CHECK_OK)
			execute_line(l);
		free_line(l);
	}
}

int main()
{
	const char * const prompt_suffix = " $ ";
	const size_t prompt_suffix_len = strlen(prompt_suffix);
	for(;;) {
		char *pwd;
		/* Make pwd point to a string containing the current working directory.
		 * The memory area must be allocated (directly or indirectly) via malloc.
		 */
		/*** TO BE DONE START ***/
		
		// Se non si conosce la dimensione della directory corrente, si può scrivere getcwd(NULL, 0) 
		// per farlo funzionare come se fossse una malloc di dimensione "quanto basta"
		if((pwd = getcwd(NULL, 0)) == NULL)
			fatal_errno("getcwd() error in main");
		
		/*** TO BE DONE END ***/
		pwd = my_realloc(pwd, strlen(pwd) + prompt_suffix_len + 1);
		strcat(pwd, prompt_suffix);
		char * const line = readline(pwd);
		free(pwd);
		if (!line) break;
		execute(line);
		free(line);
	}
}

