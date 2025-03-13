import subprocess
import os
import sys
import time

SHELL_PATH = "./sshell"  # Adjust if the path differs

def run_test(input_commands, expected_outputs, should_exit=True, check_stderr=False):
    """
    Runs the shell with a sequence of commands, capturing output and verifying correctness.
    """
    if not os.path.isfile(SHELL_PATH):
        print(f"Error: {SHELL_PATH} not found.", file=sys.stderr)
        sys.exit(1)

    process = subprocess.Popen(
        [SHELL_PATH],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True
    )

    input_text = "\n".join(input_commands) + "\n"
    stdout, stderr = process.communicate(input_text)

    output_to_check = stderr if check_stderr else stdout

    # Debugging: Print actual outputs when a test fails
    for expected in expected_outputs:
        if expected not in output_to_check:
            print("\n=== TEST FAILED ===")
            print("Expected:", repr(expected))
            print("Actual Output:", repr(output_to_check))
            print("===================\n")
            raise AssertionError(f"Test failed: Expected '{expected}' not found in output.")

    if should_exit and process.returncode != 0:
        print("\n=== TEST FAILED ===")
        print(f"Shell did not exit cleanly. Return code: {process.returncode}")
        print("===================\n")
        raise AssertionError("Shell did not exit cleanly")

    print("Test passed.")
def test_command_execution():
    """Test basic command execution."""
    run_test(["echo Hello, world!", "exit"], ["Hello, world!"])

def test_io_redirection():
    """Test input/output redirection."""
    # Test output redirection
    run_test(["echo Test > output.txt", "cat output.txt", "exit"], ["Test"])
    assert os.path.exists("output.txt"), "Output redirection failed"

    # Cleanup
    os.remove("output.txt")

def test_job_control():
    """Test background processes and job listing."""
    run_test(["sleep 5 &", "jobs", "exit"], ["Started: sleep"])

def test_pipes():
    """Test piping commands."""
    run_test(["echo hello | grep h", "exit"], ["hello"])

def test_edge_cases():
    """Test empty input and invalid commands."""
    run_test(["", "invalidcommand", "exit"], ["shell>"], check_stderr=False)  # Check stdout for shell prompt
    run_test(["", "invalidcommand", "exit"], ["execvp failed: No such file or directory"], check_stderr=True)  # Check stderr for error message

def test_exit():
    """Ensure shell exits properly."""
    run_test(["exit"], [], should_exit=True)

if __name__ == "__main__":
    test_command_execution()
    test_io_redirection()
    test_job_control()
    test_pipes()
    test_edge_cases()
    test_exit()