# myshell
A C based implementation of a shell

Author: Marco Ferreira - maf407

For interactive usage run `./mysh`

## Testing Strategy 
My testing strategy relied on breaking down each feature and testing each script in both interactive mode and batch mode.

You can find my testing scripts in `testdir/` as `.txt` files.

The following will be an overview of each script.

## built_in.txt
---
This script tests the built in functions for mysh.

### pwd Command Testing
1. Basic functionality
   - Shows current working directory display
2. Error handling
   - Tests invalid case with extra arguments, should still print

### cd Command Testing
1. Basic Navigation
   - Relative paths (testdir, dir1)
   - Parent directory (..)
   - Root directory (/)

2. Error Cases
   - No arguments
   - Nonexistent directory
   - Too many arguments

### which Command Testing
1. System Commands
   - Finding executable locations (ls)

2. Built-in Commands
   - Identifying shell built-ins (cd, pwd)

3. Error Cases
   - Nonexistent commands
   - Multiple arguments

### Usage
Run the test script in batch mode:
```bash
./mysh < testdir/built_in.txt
```

## edge_cases.txt
---
This section covers some edge cases that can occur with `mysh`

### Long Command Testing
1. Excessive Arguments
  - Tests command with extremely long filename/argument
  - Verifies buffer handling
  - Tests command line parsing for long inputs

### Whitespace Handling
1. Multiple Spaces
  - Tests command parsing with excessive spaces between arguments
  - Verifies proper argument separation
  - Ensures correct command execution regardless of spacing

### Complex Command Combinations
1. Pipeline Testing
  - Multiple pipe operations
  - Input/output redirection
  - Wildcard expansion
  - Pattern matching
  - Sorting operations

### Expected Results
1. Long commands should be processed correctly
2. Multiple spaces should be handled as single separators correctly
3. Complex pipelines should execute in correct order
4. File operations should complete successfully
5. Output should be properly redirected and formatted

### Test Coverage
- Buffer overflow prevention
- Whitespace parsing
- File manipulation
- Pipeline execution
- Redirection handling
- Wildcard processing
- Command chaining

### Usage
Run the test script in batch mode:
```bash
./mysh < testdir/edge_cases.txt
```

## error.txt
---
This section covers a script designed to give errors to verify error handling is working.

### Syntax Error Tests
- Empty redirections (>, <, |)
- Incomplete commands with redirections
- Invalid pipe usage
- Redirection with built-in commands

### Permission Error Tests
1. Execution Permission
  - Tests non-executable script access

2. Read Permission
  - Tests access to non-readable files
  - Input redirection with restricted files

3. Write Permission
  - Tests writing to read-only files
  - Output redirection to protected files

### Invalid Operation Tests
1. Missing Files/Directories
  - Nonexistent directory navigation
  - Nonexistent file access

2. Invalid Arguments
  - Too many arguments for built-ins
  - Missing required arguments

### Usage
```bash
./mysh < testdir/error_tests.txt
```

## exec.txt
---
This script tests if the mysh handles external commands properly.
### Basic Command Tests
- Simple ls command
- ls with single argument (-l)
- ls with multiple arguments (-la)
- ls with absolute path (/bin/ls)

### Invalid Command Tests
- Nonexistent command execution
- Nonexistent script execution

### Usage
```bash
./mysh < testdir/exec.txt
```

## pipes.txt
---
This script tests pipe functionality though basic pipes
### Basic Pipe Tests
- Command output counting (ls | wc -l)
- Content filtering (cat | grep)
- Simple text processing

### Error Case Tests
- Invalid first command in pipe
- Invalid second command in pipe
- Built-in command restrictions

### Redirection with Pipes
- Input redirection with pipes
- Output redirection with pipes
- Multiple operations (input, pipe, output)

## Usage
```bash
./mysh < testdir/pipes.txt
```

## redirection.txt
---
This script tests both input and output redirection functionality
### Input Redirection Tests
- Reading from existing files
- Attempting to read nonexistent files
- Sorting input from files

### Output Redirection Tests
- Creating new files
- Overwriting existing files
- Redirecting command output
- Error output handling

### Combined Redirection Tests
- Input and output redirection together
- File processing with both redirections

## Usage
```bash
./mysh < testdir/redirection.txt
```
## wildcards.txt
---
This script tests wildcard expantion functionality


### Basic Wildcard Tests
- All text files (*.txt)
- Files with specific prefix (file*)
- Combined prefix/suffix (fi*.txt)
- Different extensions (*dat)
- Directory-specific patterns (dir1/*)
- Non-matching patterns

### Multiple Pattern Tests
- Multiple extensions (*.txt *.dat)
- Patterns in different directories
- Combined pattern matching

### Hidden File Tests
- Explicit hidden files (.*)
- General wildcard behavior with hidden files

### Usage
```bash
./mysh < testdir/wildcard.txt
```
