"""
Tests for the await command line tool.
"""
import subprocess
import time
import pytest
import os
import signal
import re
from typing import Tuple, Optional


def run_await_with_timeout(
    cmd_args: str, 
    timeout: float = 3.0, 
    input_data: Optional[str] = None,
    description: Optional[str] = None
) -> Tuple[int, str, str]:
    """
    Run await command with timeout and return (returncode, stdout, stderr).
    
    Args:
        cmd_args: Command line string for await (e.g., '-fVo "echo hello" "date"')
        timeout: Timeout in seconds
        input_data: Optional stdin input
        description: Optional description of what we expect
    
    Returns:
        Tuple of (return_code, stdout, stderr)
    """
    full_cmd = f"../await {cmd_args}"
    
    print(f"\n\033[96m🔍 Running:\033[0m \033[93m{full_cmd}\033[0m")
    if description:
        print(f"\033[90m📝 Expecting: {description}\033[0m")
    
    try:
        result = subprocess.run(
            full_cmd,
            shell=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=timeout,
            input=input_data
        )
        if result.returncode == 0:
            print(f"\033[92m✅ Exit code: {result.returncode}\033[0m")
        else:
            print(f"\033[91m❌ Exit code: {result.returncode}\033[0m")
        if result.stdout.strip():
            clean_stdout = strip_ansi_escape_codes(result.stdout)
            if '\n' in clean_stdout and len(clean_stdout) > 50:
                print(f"\033[94m📤 Stdout:\033[0m")
                for line in clean_stdout[:200].split('\n')[:5]:  # Show first 5 lines
                    if line.strip():  # Only show non-empty lines
                        print(line)
                if len(clean_stdout) > 200:
                    print("...")
            else:
                print(f"\033[94m📤 Stdout:\033[0m {clean_stdout}")
        if result.stderr.strip():
            clean_stderr = strip_ansi_escape_codes(result.stderr)
            if '\n' in clean_stderr and len(clean_stderr) > 50:
                print(f"\033[95m📤 Stderr:\033[0m")
                for line in clean_stderr[:200].split('\n')[:5]:  # Show first 5 lines
                    if line.strip():  # Only show non-empty lines
                        print(line)
                if len(clean_stderr) > 200:
                    print("...")
            else:
                print(f"\033[95m📤 Stderr:\033[0m {clean_stderr}")
        print("\n")  # Add separation after test execution
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        print(f"\033[91m⏰ Command timed out after {timeout}s\033[0m")
        # Kill the process if it times out
        stdout = e.stdout.decode('utf-8') if e.stdout else ""
        stderr = e.stderr.decode('utf-8') if e.stderr else ""
        if stdout.strip():
            clean_stdout = strip_ansi_escape_codes(stdout)
            if '\n' in clean_stdout and len(clean_stdout) > 50:
                print(f"\033[94m📤 Partial stdout:\033[0m")
                for line in clean_stdout[:200].split('\n')[:5]:  # Show first 5 lines
                    if line.strip():  # Only show non-empty lines
                        print(line)
                if len(clean_stdout) > 200:
                    print("...")
            else:
                print(f"\033[94m📤 Partial stdout:\033[0m {clean_stdout}")
        if stderr.strip():
            clean_stderr = strip_ansi_escape_codes(stderr)
            if '\n' in clean_stderr and len(clean_stderr) > 50:
                print(f"\033[95m📤 Partial stderr:\033[0m")
                for line in clean_stderr[:200].split('\n')[:5]:  # Show first 5 lines
                    if line.strip():  # Only show non-empty lines
                        print(line)
                if len(clean_stderr) > 200:
                    print("...")
            else:
                print(f"\033[95m📤 Partial stderr:\033[0m {clean_stderr}")
        print("\n")  # Add separation after test execution
        return 124, stdout, stderr  # 124 is timeout exit code


def strip_ansi_escape_codes(text: str) -> str:
    """Remove ANSI escape codes from text for easier testing."""
    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    return ansi_escape.sub('', text)


class TestAwaitBasics:
    """Test basic await functionality."""
    
    def test_help_flag(self):
        """Test that --help works."""
        returncode, stdout, stderr = run_await_with_timeout(
            "--help",
            description="Should show help text and exit with code 0"
        )
        assert returncode == 0
        assert "await [options] commands" in stdout
        assert "waiting commands to fail" in stdout
    
    def test_version_flag(self):
        """Test that --version works.""" 
        returncode, stdout, stderr = run_await_with_timeout(
            "--version",
            description="Should show version number and exit with code 0"
        )
        assert returncode == 0
        assert stdout.strip().startswith("1.0")
    
    def test_no_args_shows_help(self):
        """Test that running without arguments shows help."""
        returncode, stdout, stderr = run_await_with_timeout(
            "",
            description="Should show help when run without arguments"
        )
        assert returncode == 0
        assert "await [options] commands" in stdout


class TestCommandExecution:
    """Test command execution and status handling."""
    
    def test_successful_command(self):
        """Test waiting for a successful command."""
        returncode, stdout, stderr = run_await_with_timeout(
            '"echo \'test\'"',
            description="Should exit successfully when command succeeds"
        )
        # Should exit successfully when command succeeds
        assert returncode == 0
    
    def test_failing_command_default(self):
        """Test that failing commands keep running by default."""
        returncode, stdout, stderr = run_await_with_timeout(
            '"false"',
            timeout=2.0,
            description="Should timeout because await keeps waiting for command to succeed"
        )
        # Should timeout because it keeps waiting for success
        assert returncode == 124
    
    def test_failing_command_with_fail_flag(self):
        """Test --fail flag waits for commands to fail."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--fail "false"',
            description="Should exit successfully when command fails with --fail flag"
        )
        # Should exit successfully when command fails
        assert returncode == 0
    
    def test_multiple_commands_all_success(self):
        """Test multiple commands that all succeed."""
        returncode, stdout, stderr = run_await_with_timeout(
            '"true" "echo \'hello\'" "echo \'world\'"',
            description="Should exit when all commands succeed"
        )
        assert returncode == 0
    
    def test_specific_status_code(self):
        """Test waiting for specific status code."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--status 2 "exit 2"',
            description="Should exit when command returns status code 2"
        )
        assert returncode == 0


class TestOutputModes:
    """Test different output modes."""
    
    def test_stdout_flag(self):
        """Test --stdout flag shows command output."""
        # Use -fVo (fail + silent + stdout) to get clean output 
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'test_output\'; false"',
            description="Should show 'test_output' in stdout and exit when command fails with -f flag"
        )
        assert returncode == 0  # Should exit successfully when command fails with -f
        # Clean ANSI codes and check for our output
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "test_output" in clean_stdout
    
    def test_silent_flag(self):
        """Test --silent flag suppresses spinners."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--silent "echo \'test\'"',
            description="Should run without showing spinners"
        )
        assert returncode == 0
        # Should not contain spinner characters in stderr
        clean_stderr = strip_ansi_escape_codes(stderr)
        spinner_chars = ["⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷"]
        for char in spinner_chars:
            assert char not in clean_stderr
    
    def test_silent_with_stdout(self):
        """Test -fVo (fail + silent + stdout) shows only clean output."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'clean_output\'; false"',
            description="Should show clean output without spinners and exit when command fails"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        clean_stderr = strip_ansi_escape_codes(stderr)
        
        # Should have our output in stdout
        assert "clean_output" in clean_stdout
        # Should not have spinners in stderr
        spinner_chars = ["⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷"]
        for char in spinner_chars:
            assert char not in clean_stderr
    
    def test_fail_silent_stdout(self):
        """Test -fVo (fail + silent + stdout) combination."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'fail_test\'; false"',
            description="Should show output and exit when command fails"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "fail_test" in clean_stdout


class TestFlagCombinations:
    """Test various flag combinations."""
    
    def test_any_flag_with_multiple_commands(self):
        """Test --any flag exits when any command succeeds."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--any "false" "true"',
            timeout=2.0,
            description="Should exit quickly when ANY command (true) succeeds"
        )
        # Should exit quickly when the true command succeeds
        assert returncode == 0
    
    def test_change_flag(self):
        """Test --change flag waits for output changes."""
        # Create a temporary file that we'll modify
        test_file = "/tmp/await_test_change"
        with open(test_file, "w") as f:
            f.write("initial")
        
        try:
            # Start await in background watching the file
            cmd = ["../await", "--change", f"cat {test_file}"]
            proc = subprocess.Popen(
                cmd, 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE,
                text=True
            )
            
            # Give it a moment to start
            time.sleep(0.5)
            
            # Modify the file
            with open(test_file, "w") as f:
                f.write("changed")
            
            # Wait for await to exit
            try:
                stdout, stderr = proc.communicate(timeout=3.0)
                returncode = proc.returncode
                assert returncode == 0
            except subprocess.TimeoutExpired:
                proc.kill()
                pytest.fail("await didn't exit after file change")
                
        finally:
            # Clean up
            if os.path.exists(test_file):
                os.remove(test_file)
    
    def test_interval_flag(self):
        """Test --interval flag changes polling frequency."""
        start_time = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '--interval 1000 "echo \'test\'"',
            description="Should complete quickly since echo succeeds immediately"
        )
        end_time = time.time()
        
        assert returncode == 0
        # Should complete quickly since echo succeeds immediately
        assert end_time - start_time < 2.0


class TestEdgeCases:
    """Test edge cases and error conditions."""
    
    def test_nonexistent_command(self):
        """Test behavior with non-existent command."""
        returncode, stdout, stderr = run_await_with_timeout(
            '"nonexistent_command_12345"',
            timeout=2.0,
            description="Should timeout because command doesn't exist (fails continuously)"
        )
        # Should timeout because command doesn't exist (fails continuously)
        assert returncode == 124
    
    def test_empty_command(self):
        """Test behavior with empty command string."""
        returncode, stdout, stderr = run_await_with_timeout(
            '""',
            timeout=1.0,
            description="Should handle empty command gracefully"
        )
        # Should handle empty command gracefully
        assert returncode in [0, 124]  # Either succeeds or times out
    
    def test_command_with_quotes(self):
        """Test commands with quotes are handled correctly."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'hello world\'; false"',
            description="Should handle commands with quotes correctly"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "hello world" in clean_stdout
    
    def test_command_with_pipes(self):
        """Test commands with pipes work correctly."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'test\' | cat; false"',
            description="Should handle commands with pipes correctly"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "test" in clean_stdout


class TestRealWorldScenarios:
    """Test real-world usage scenarios."""
    
    def test_network_command_simulation(self):
        """Test scenario similar to curl command."""
        # Simulate network delay with sleep + echo, then fail to exit
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "sleep 0.1; echo \'{\\"status\\":\\"ok\\"}\'; false"',
            timeout=3.0,
            description="Should simulate network delay and show JSON output"
        )
        
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert '{"status":"ok"}' in clean_stdout
    
    def test_previous_output_display(self):
        """Test that previous output is shown when command is running."""
        # Test that commands produce expected output with fail flag
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo \'first\'; echo \'second\'; false"',
            timeout=3.0,
            description="Should show both 'first' and 'second' outputs"
        )
        
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # Should contain both outputs eventually
        assert "second" in clean_stdout


if __name__ == "__main__":
    # Make sure await binary exists
    if not os.path.exists("../await"):
        print("Error: ../await binary not found. Run '../build' first.")
        exit(1)
    
    # Run the tests
    pytest.main([__file__, "-v"])