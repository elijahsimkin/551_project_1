# C Shell for Minux
A lightweight Minux-targeted shell implemented in C with support for command execution, piping, redirection, background jobs, and built-in commands.

---

## 🎮 Features 

- Execute standard Unix commands (e.g., `ls`, `cd`, `grep`, etc.)
- Input/output redirection (`>`, `<`, `>>`)
- Pipelining (`|`)
- Background execution (`&`)
- Built-in commands: `cd`, `exit`, `jobs`, `fg`, `bg`
- Signal handling (e.g., `Ctrl+C`, `Ctrl+Z`)
- Job control with process tracking
---

## 📸 Demo



## 📂 Structure
```
.
├── main.c         // Entry point
├── parser.c       // Command parsing logic
├── executor.c     // Forking, piping, redirection
├── jobs.c         // Job control and signal handling
├── Makefile
└── README.md
```
---

## 🧪 How to Run



## 🙋 Group Members
	-	Eli Simkin
	-	Likitha S
  - Sohini Sahukar

## 🧠 What We Learned
	•	Low-level process management using fork, exec, wait, and kill
	•	Parsing CLI input with whitespace and operators
	•	Implementing background process management and job tracking
	•	Handling Unix signals and writing modular C code

## 📈 Future Improvements
	•	Implement command history with arrow key support
	•	Add scripting support and configuration files
	•	Improve error handling and edge case robustness
