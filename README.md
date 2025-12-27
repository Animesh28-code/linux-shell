# Linux Shell in C

A custom Linux shell implemented in C that supports command execution, built-in commands, piping, and I/O redirection.

## Features
- Command execution using fork() and execvp()
- Built-in commands: cd, exit, help
- Piping (|)
- Input redirection (<)
- Output redirection (>)
- Append redirection (>>)
- Command history using GNU Readline
- Custom colored prompt

## Requirements
- Linux / WSL
- GCC
- GNU Readline

## Installation
```bash
sudo apt install build-essential libreadline-dev

