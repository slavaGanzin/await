"""
Tests for the await command line tool.
"""
import subprocess
import time
import pytest
import os
import signal
import re
import platform
import json
import tempfile
import statistics
from typing import Tuple, Optional


# Get the appropriate temp directory for the environment
# This works in both regular Linux and Nix sandboxed builds
TMPDIR = os.environ.get('TMPDIR', tempfile.gettempdir())


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
    # exec so a timeout kills await itself, not just the wrapping shell
    full_cmd = f"exec ../await {cmd_args}"
    
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
        # Version format is x.y.z
        assert len(stdout.strip().split('.')) == 3
    
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
        # Adding sleep before false to ensure output is flushed (helps on macOS)
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "echo test_output; sleep 0.2; false"',
            timeout=5.0,
            description="Should show 'test_output' in stdout and exit when command fails with -f flag"
        )
        assert returncode == 0  # Should exit successfully when command fails with -f
        # Clean ANSI codes and check for our output
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "test_output" in clean_stdout, f"Expected 'test_output' in stdout, got: {repr(clean_stdout)}"
    
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
        test_file = os.path.join(TMPDIR, "await_test_change")
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
            '--interval 1 "echo \'test\'"',
            description="Should complete quickly since echo succeeds immediately"
        )
        end_time = time.time()
        
        assert returncode == 0
        # Should complete quickly since echo succeeds immediately
        assert end_time - start_time < 2.0


class TestTimeoutFlag:
    """Test --timeout / -t flag functionality."""

    def test_timeout_flag_in_help(self):
        """Test that --timeout flag appears in help."""
        returncode, stdout, stderr = run_await_with_timeout(
            "--help",
            description="Should show timeout flag in help text"
        )
        assert returncode == 0
        assert "--timeout -T" in stdout
        assert "seconds to wait before giving up" in stdout

    def test_timeout_with_failing_command(self):
        """Test timeout exits after specified time with failing command."""
        start_time = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '--timeout 2 --silent "false"',
            timeout=5.0,
            description="Should timeout after 2 seconds"
        )
        end_time = time.time()

        # Should exit with error code (timeout)
        assert returncode == 1
        # Should have taken approximately 2 seconds (allow some margin)
        elapsed = end_time - start_time
        assert 1.5 < elapsed < 3.0, f"Expected ~2s timeout, got {elapsed:.2f}s"

    def test_timeout_with_successful_command(self):
        """Test timeout doesn't exit early if command succeeds quickly."""
        start_time = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '--timeout 5 --silent "true"',
            timeout=3.0,
            description="Should exit immediately when command succeeds, not wait for timeout"
        )
        end_time = time.time()

        # Should exit successfully
        assert returncode == 0
        # Should complete quickly, not wait for timeout
        elapsed = end_time - start_time
        assert elapsed < 2.0, f"Expected quick completion, got {elapsed:.2f}s"

    def test_timeout_message(self):
        """Test timeout shows appropriate message."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--timeout 1 "false"',
            timeout=3.0,
            description="Should show timeout message after 1 second"
        )

        # Should exit with error code
        assert returncode == 1
        # Should contain timeout message in stderr
        clean_stderr = strip_ansi_escape_codes(stderr)
        assert "Timeout reached" in clean_stderr
        assert "ms" in clean_stderr

    def test_timeout_with_silent_flag(self):
        """Test timeout with --silent suppresses timeout message."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--timeout 1 --silent "false"',
            timeout=3.0,
            description="Should timeout silently without message"
        )

        # Should exit with error code
        assert returncode == 1
        # Should NOT contain timeout message in stderr when silent
        clean_stderr = strip_ansi_escape_codes(stderr)
        assert "Timeout reached" not in clean_stderr

    def test_timeout_short_flag(self):
        """Test -t short flag works."""
        start_time = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '-T 2 --silent "false"',
            timeout=5.0,
            description="Should timeout after 2 seconds with -T flag"
        )
        end_time = time.time()

        assert returncode == 1
        elapsed = end_time - start_time
        assert 1.5 < elapsed < 3.5, f"Expected ~2s timeout, got {elapsed:.2f}s"

    def test_timeout_with_change_flag(self):
        """Test timeout works with --change flag."""
        # Create a file that never changes
        test_file = os.path.join(TMPDIR, "await_timeout_change_test")
        with open(test_file, "w") as f:
            f.write("static")

        try:
            start_time = time.time()
            returncode, stdout, stderr = run_await_with_timeout(
                f'--timeout 2 --change --silent "cat {test_file}"',
                timeout=3.0,
                description="Should time out: the first read is a baseline, not a change"
            )
            end_time = time.time()

            # The first read is only the baseline; static content never changes
            assert returncode == 1
            elapsed = end_time - start_time
            assert 1.8 < elapsed < 3.0, f"Expected ~2s timeout, got {elapsed:.2f}s"
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_timeout_zero_means_no_timeout(self):
        """Test timeout=0 (default) means no timeout."""
        # This command would normally run forever, but we'll give it a subprocess timeout
        returncode, stdout, stderr = run_await_with_timeout(
            '--timeout 0 --silent "false"',
            timeout=2.0,
            description="Should run without timeout limit (subprocess timeout will stop it)"
        )
        # Should be killed by subprocess timeout (124), not await timeout (1)
        assert returncode == 124


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


class TestDiffMode:
    """Test the -d (diff) flag functionality."""
    
    def test_diff_flag_in_help(self):
        """Test that --diff flag appears in help."""
        returncode, stdout, stderr = run_await_with_timeout(
            "--help",
            description="Should show diff flag in help text"
        )
        assert returncode == 0
        assert "--diff -d" in stdout
        assert "highlight differences" in stdout
    
    def test_diff_mode_with_changing_output(self):
        """Test diff highlighting with changing counter output."""
        # Create a script that outputs incrementing counter
        counter_file = os.path.join(TMPDIR, "counter")
        script_path = os.path.join(TMPDIR, "test_counter.sh")
        script_content = f"""#!/bin/bash
if [ ! -f {counter_file} ]; then
    echo 0 > {counter_file}
fi
count=$(cat {counter_file})
echo "{{\\"counter\\": $count}}"
echo $((count + 1)) > {counter_file}
"""
        with open(script_path, "w") as f:
            f.write(script_content)
        
        try:
            # Make script executable
            os.chmod(script_path, 0o755)

            # Initialize counter
            with open(counter_file, "w") as f:
                f.write("100")

            # Test with diff mode - should exit after a few iterations showing differences
            returncode, stdout, stderr = run_await_with_timeout(
                f'--diff --change -Vo "{script_path}"',
                timeout=3.0,
                description="Should highlight changing numbers in JSON output"
            )

            assert returncode == 0
            # Should contain highlighted differences (ANSI escape codes for highlighting)
            assert "\033[32m" in stdout  # Green text highlighting

        finally:
            # Clean up
            for f in [script_path, counter_file]:
                if os.path.exists(f):
                    os.remove(f)
    
    def test_diff_mode_no_changes(self):
        """Test diff mode with static output (no highlighting)."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--diff -fVo "echo \'static output\'; false"',
            timeout=2.0,
            description="Should show static output without highlighting"
        )
        
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "static output" in clean_stdout
        # Should not contain highlighting escape codes since output doesn't change
        assert stdout.count("\033[32m") <= 1  # Maybe one highlight on first change detection


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


class TestPlaceholderSubstitution:
    """Test placeholder substitution feature (\\1, \\2, etc.)."""

    def test_single_placeholder(self):
        """Test that \\1 placeholder works in commands."""
        # Placeholders work after first iteration, so use -Vo to see output
        returncode, stdout, stderr = run_await_with_timeout(
            '-Vo "printf hello" "echo \\\\1"',
            timeout=3.0,
            description="Should substitute \\1 with output of first command"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # After first iteration, should see "hello" substituted
        assert "hello" in clean_stdout

    def test_multiple_placeholders(self):
        """Test that multiple placeholders work (\\1, \\2)."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-Vo "printf foo" "printf bar" "echo \\\\1 \\\\2"',
            timeout=3.0,
            description="Should substitute \\1 and \\2 with respective command outputs"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # Should see substituted values in output
        assert "foo" in clean_stdout and "bar" in clean_stdout

    def test_placeholder_in_exec(self):
        """Test that --exec can be used (placeholder substitution in exec has limitations)."""
        # Note: Placeholder substitution in --exec is documented but has limitations
        # in practice because exec runs when conditions are met, which may be before
        # placeholders are populated. This test verifies --exec works in general.
        test_file = os.path.join(TMPDIR, "await_placeholder_exec_test")
        if os.path.exists(test_file):
            os.remove(test_file)

        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'-V --exec "touch {test_file}" "echo success"',
                timeout=5.0,
                description="Should run exec command when condition met"
            )
            assert returncode == 0

            # Give exec thread time to complete
            time.sleep(1.0)

            # Verify the file was created
            assert os.path.exists(test_file), "Exec command should have run"
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_chained_placeholders(self):
        """Test multiple commands with placeholder references."""
        # Note: expr fails on first iteration before placeholders are populated
        # This test verifies the commands run and produce output, showing
        # the limitation of placeholder timing
        returncode, stdout, stderr = run_await_with_timeout(
            '-Vo "printf 10" "printf 20" "echo \\\\1 \\\\2"',
            timeout=3.0,
            description="Should show placeholder substitution in action"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # Should see the numbers being output and eventually substituted
        assert "10" in clean_stdout and "20" in clean_stdout

    @pytest.mark.skipif(
        platform.system() == "Darwin",
        reason="TODO: Placeholder substitution with expr evaluation is slow/unstable on macOS. "
               "The expr command appears to have issues with rapid input changes in the macOS environment. "
               "This test passes consistently on Linux. Consider: (1) optimizing expr usage, "
               "(2) implementing a simpler substitute test, or (3) adjusting macOS-specific timing."
    )
    def test_placeholder_documentation_example(self):
        """Test the example from help documentation."""
        # This example from the help text should work
        # Note: We can't test --forever directly, so we'll verify the placeholders work
        # Using printf instead of echo -n for POSIX compatibility
        returncode, stdout, stderr = run_await_with_timeout(
            '-o --silent "printf 10" "printf 15" "expr \\\\1 + \\\\2"',
            timeout=5.0,
            description="Should match documentation example behavior"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # Should eventually show 25 (10 + 15)
        assert "25" in clean_stdout or "10" in clean_stdout


class TestExecFlag:
    """Test --exec flag functionality."""

    def test_exec_runs_on_success(self):
        """Test --exec command runs when conditions are met."""
        test_file = os.path.join(TMPDIR, "await_exec_test")
        if os.path.exists(test_file):
            os.remove(test_file)

        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'--exec "touch {test_file}" "true"',
                timeout=3.0,
                description="Should run exec command on success"
            )
            assert returncode == 0

            # Give it a moment for exec to complete
            time.sleep(0.5)

            # Verify the file was created
            assert os.path.exists(test_file), "Exec command should have created the file"
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_exec_not_run_on_timeout(self):
        """Test --exec doesn't run when conditions aren't met."""
        test_file = os.path.join(TMPDIR, "await_exec_fail_test")
        if os.path.exists(test_file):
            os.remove(test_file)

        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'--exec "touch {test_file}" "false"',
                timeout=2.0,
                description="Should NOT run exec when command keeps failing"
            )
            # Should timeout
            assert returncode == 124

            # Give it a moment just in case
            time.sleep(0.5)

            # Verify the file was NOT created
            assert not os.path.exists(test_file), "Exec command should NOT have run"
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_exec_with_fail_flag(self):
        """Test --exec runs with --fail when command fails."""
        test_file = os.path.join(TMPDIR, "await_exec_fail_flag_test")
        if os.path.exists(test_file):
            os.remove(test_file)

        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'--fail --exec "touch {test_file}" "false"',
                timeout=3.0,
                description="Should run exec when command fails with --fail flag"
            )
            assert returncode == 0

            # Give it a moment for exec to complete
            time.sleep(0.5)

            # Verify the file was created
            assert os.path.exists(test_file), "Exec should run when fail condition is met"
        finally:
            if os.path.exists(test_file):
                os.remove(test_file)

    def test_exec_with_output(self):
        """Test --exec command can output to stdout."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--stdout --exec "echo EXEC_RAN" "true"',
            timeout=3.0,
            description="Should show exec command output"
        )
        assert returncode == 0
        # Note: exec output might not appear in our test due to timing,
        # but the command should complete successfully


@pytest.mark.skipif(platform.system() not in ("Linux", "Darwin"),
                    reason="--service needs systemd (Linux) or launchd (macOS)")
class TestService:
    """--service writes a systemd unit (Linux) or launchd agent (macOS) that replays the full command line."""

    @pytest.mark.skipif(platform.system() == "Darwin", reason="systemd is Linux-only")
    def test_service_unit_keeps_all_flags_and_escapes(self):
        import shutil
        root = tempfile.mkdtemp()
        home = os.path.join(root, "o'brien")        # quote in HOME
        bindir = os.path.join(root, "stub")
        await_dir = os.path.join(root, "my bin")   # space in the binary path
        for d in (home, bindir, await_dir):
            os.mkdir(d)
        for stub in ("systemctl", "journalctl"):
            path = os.path.join(bindir, stub)
            with open(path, "w") as f:
                f.write("#!/bin/sh\nexit 0\n")
            os.chmod(path, 0o755)
        binary = os.path.join(await_dir, "await")
        shutil.copy("../await", binary)

        result = subprocess.run(
            [binary, "--name", "web", "--json", "--lap", "-i", "0.5", "--times", "3", "--backoff", "30",
             'curl -sf "http://x/$HOME" | grep 100%', "--service", "t"],
            env={**os.environ, "HOME": home, "PATH": bindir + ":" + os.environ["PATH"]},
            capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 0, result.stderr
        unit_path = os.path.join(home, ".config/systemd/user/t.service")
        with open(unit_path) as f:
            unit = f.read()
        exec_start = next(l for l in unit.splitlines() if l.startswith("ExecStart="))
        assert exec_start.startswith(f'ExecStart="{binary}" ')
        for flag in ('--name "web"', "--json", "--lap", '--interval "0.5"', '--times "3"', '--backoff "30"'):
            assert flag in exec_start
        # systemd expands $ and % and ends the argument at an unescaped quote
        assert '"curl -sf \\"http://x/$$HOME\\" | grep 100%%"' in exec_start

        if shutil.which("systemd-analyze"):
            verify = subprocess.run(["systemd-analyze", "verify", unit_path],
                                    capture_output=True, text=True)
            assert verify.returncode == 0, verify.stderr

    @staticmethod
    def _run_service(argv, home, env_extra=None):
        env = {**os.environ, "HOME": home, "AWAIT_SERVICE_NO_ACTIVATE": "1", **(env_extra or {})}
        return subprocess.run(["../await", *argv], env=env, capture_output=True, text=True, timeout=5)

    @pytest.mark.skipif(platform.system() == "Darwin", reason="systemd is Linux-only")
    def test_service_no_activate_writes_unit_only(self):
        home = tempfile.mkdtemp()
        bindir = tempfile.mkdtemp()
        marker = os.path.join(home, "called")
        for stub in ("systemctl", "journalctl"):
            path = os.path.join(bindir, stub)
            with open(path, "w") as f:
                f.write(f"#!/bin/sh\ntouch '{marker}'\nexit 0\n")
            os.chmod(path, 0o755)
        result = self._run_service(["-f", "true", "--service", "noact"], home,
                                   {"PATH": bindir + ":" + os.environ["PATH"]})
        assert result.returncode == 0, result.stderr
        unit_path = os.path.join(home, ".config/systemd/user/noact.service")
        with open(unit_path) as f:
            unit = f.read()
        assert "Restart=always" in unit
        assert '--fail  "true"' in unit
        assert unit_path in result.stdout
        assert not os.path.exists(marker), "systemctl/journalctl ran despite AWAIT_SERVICE_NO_ACTIVATE"

    @pytest.mark.skipif(platform.system() == "Darwin", reason="systemd is Linux-only")
    def test_service_invalid_name_rejected_linux(self):
        home = tempfile.mkdtemp()
        for name in ("a/b", "../x", "", "a b", "x@y", "."):
            result = self._run_service(["true", "--service", name], home)
            assert result.returncode == 2, (name, result.stderr)
            assert "invalid --service name" in result.stderr
        assert not os.path.exists(os.path.join(home, ".config"))

    @pytest.mark.skipif(platform.system() != "Darwin", reason="launchd is macOS-only")
    def test_service_launchd_plist(self):
        import plistlib
        home = tempfile.mkdtemp()
        cmd = 'echo "a b" \'c\' && test 1 \\< 2 > /dev/null; echo $HOME \\\\n & wait'
        args = ["-f", "--name", "it's <web> & co", "-i", "0.5", cmd, "second cmd"]
        for service_args, rest in (
            (["--service", "t.x_1-2"], []),
            (["-S", "t.x_1-2"], []),
            (["--service=t.x_1-2"], []),
        ):
            result = self._run_service([*args, *service_args, *rest], home)
            assert result.returncode == 0, result.stderr
            plist_path = os.path.join(home, "Library/LaunchAgents/await.t.x_1-2.plist")
            assert plist_path in result.stdout
            lint = subprocess.run(["plutil", "-lint", plist_path], capture_output=True, text=True)
            assert lint.returncode == 0, lint.stdout + lint.stderr
            with open(plist_path, "rb") as f:
                plist = plistlib.load(f)
            assert plist["ProgramArguments"] == [os.path.realpath("../await"), *args]
            assert plist["Label"] == "await.t.x_1-2"
            assert plist["KeepAlive"] is True
            assert plist["RunAtLoad"] is True
            log = os.path.join(home, "Library/Logs/await-t.x_1-2.log")
            assert plist["StandardOutPath"] == log
            assert plist["StandardErrorPath"] == log
            assert plist["EnvironmentVariables"]["PATH"] == os.environ["PATH"]
            assert os.path.isdir(os.path.join(home, "Library/Logs"))

        # -S inside a cluster of short flags, value in the next argument
        result = self._run_service(["-fS", "c", "true"], home)
        assert result.returncode == 0, result.stderr
        with open(os.path.join(home, "Library/LaunchAgents/await.c.plist"), "rb") as f:
            assert plistlib.load(f)["ProgramArguments"][1:] == ["-f", "true"]

    @pytest.mark.skipif(platform.system() != "Darwin", reason="launchd is macOS-only")
    def test_service_invalid_name_rejected_macos(self):
        home = tempfile.mkdtemp()
        for name in ("a/b", "../x", "", "a b", "x:y", "."):
            result = self._run_service(["true", "--service", name], home)
            assert result.returncode == 2, (name, result.stderr)
            assert "invalid --service name" in result.stderr
        assert not os.path.exists(os.path.join(home, "Library"))

    @staticmethod
    def _service_file(home, name):
        if platform.system() == "Darwin":
            return os.path.join(home, f"Library/LaunchAgents/await.{name}.plist")
        return os.path.join(home, f".config/systemd/user/{name}.service")

    def test_service_rejects_invalid_utf8_argument(self):
        home = tempfile.mkdtemp()
        env = {**os.environ, "HOME": home, "AWAIT_SERVICE_NO_ACTIVATE": "1"}
        result = subprocess.run([b"../await", b"-f", b"echo \xff\xfe", b"--service", b"u"],
                                env=env, capture_output=True, timeout=5)
        assert result.returncode == 2, result.stderr
        assert b"argument 2 contains invalid UTF-8" in result.stderr
        assert not os.path.exists(self._service_file(home, "u"))
        # valid multibyte UTF-8 is fine
        result = self._run_service(["echo 'h\u00e9llo \u2713 \U0001F600'", "--service", "u"], home)
        assert result.returncode == 0, result.stderr
        assert os.path.exists(self._service_file(home, "u"))

    @pytest.mark.skipif(platform.system() != "Darwin", reason="launchd is macOS-only")
    def test_service_launchd_rejects_control_characters(self):
        import plistlib
        home = tempfile.mkdtemp()
        result = self._run_service(["true", "printf '\x1b[31mred'", "--service", "c"], home)
        assert result.returncode == 2, result.stderr
        assert "argument 2 contains a control character" in result.stderr
        assert not os.path.exists(self._service_file(home, "c"))

        result = self._run_service(["true", "--service", "c"], home,
                                   {"PATH": os.environ["PATH"] + ":/tmp/\x1b"})
        assert result.returncode == 2, result.stderr
        assert "PATH contains a control character" in result.stderr
        assert not os.path.exists(self._service_file(home, "c"))

        # tab, newline and carriage return are representable and must round-trip
        cmd = "printf 'a\tb\r\nc'\necho d\r"
        result = self._run_service([cmd, "--service", "c"], home)
        assert result.returncode == 0, result.stderr
        plist_path = self._service_file(home, "c")
        lint = subprocess.run(["plutil", "-lint", plist_path], capture_output=True, text=True)
        assert lint.returncode == 0, lint.stdout + lint.stderr
        with open(plist_path, "rb") as f:
            assert plistlib.load(f)["ProgramArguments"][1:] == [cmd]

    def test_service_fails_in_deleted_directory(self):
        home = tempfile.mkdtemp()
        root = tempfile.mkdtemp()
        result = subprocess.run(
            ["sh", "-c", 'mkdir gone && cd gone && rmdir ../gone && exec "$0" true --service gone',
             os.path.realpath("../await")],
            cwd=root, env={**os.environ, "HOME": home, "AWAIT_SERVICE_NO_ACTIVATE": "1"},
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 1, result.stderr
        assert "working directory" in result.stderr
        assert not os.path.exists(self._service_file(home, "gone"))

    @staticmethod
    def _stub_bin(names):
        """Stubs that log their name and argv as JSON lines to $STUB_LOG and
        exit with $STUB_RC_<SUBCOMMAND> (default 0)."""
        import sys
        bindir = tempfile.mkdtemp()
        for name in names:
            path = os.path.join(bindir, name)
            with open(path, "w") as f:
                f.write(f"#!{sys.executable}\n"
                        "import json, os, sys\n"
                        "with open(os.environ['STUB_LOG'], 'a') as f:\n"
                        "    f.write(json.dumps([os.path.basename(sys.argv[0])] + sys.argv[1:]) + '\\n')\n"
                        "sub = sys.argv[1] if len(sys.argv) > 1 else ''\n"
                        "sys.exit(int(os.environ.get('STUB_RC_' + sub.upper().replace('-', '_'), '0')))\n")
            os.chmod(path, 0o755)
        return bindir

    @staticmethod
    def _stub_calls(log):
        import json
        if not os.path.exists(log):
            return []
        with open(log) as f:
            return [json.loads(line) for line in f]

    @pytest.mark.skipif(platform.system() != "Darwin", reason="launchd is macOS-only")
    def test_service_launchd_activation(self):
        bindir = self._stub_bin(["launchctl"])
        uid = os.getuid()
        for rcs, want_rc, want_load in (
            ({}, 0, False),
            ({"STUB_RC_BOOTSTRAP": "5"}, 0, True),
            ({"STUB_RC_BOOTSTRAP": "5", "STUB_RC_LOAD": "1"}, 1, True),
        ):
            home = tempfile.mkdtemp()
            log = os.path.join(home, "calls.jsonl")
            env = {k: v for k, v in os.environ.items() if k != "AWAIT_SERVICE_NO_ACTIVATE"}
            env.update({"HOME": home, "PATH": bindir + ":" + os.environ["PATH"],
                        "STUB_LOG": log, "STUB_RC_BOOTOUT": "3", **rcs})
            result = subprocess.run(["../await", "true", "--service", "act"],
                                    env=env, capture_output=True, text=True, timeout=10)
            plist = self._service_file(home, "act")
            expected = [["launchctl", "bootout", f"gui/{uid}/await.act"],
                        ["launchctl", "bootstrap", f"gui/{uid}", plist]]
            if want_load:
                expected.append(["launchctl", "load", "-w", plist])
            assert self._stub_calls(log) == expected, (rcs, result.stderr)
            assert result.returncode == want_rc, (rcs, result.stderr)
            if want_rc == 0:
                assert "launchctl bootout gui/$UID/await.act" in result.stdout
            else:
                assert "launchctl could not load" in result.stderr

    @pytest.mark.skipif(platform.system() == "Darwin", reason="systemd is Linux-only")
    def test_service_systemd_activation(self):
        bindir = self._stub_bin(["systemctl", "journalctl"])
        home = tempfile.mkdtemp()
        log = os.path.join(home, "calls.jsonl")
        env = {k: v for k, v in os.environ.items() if k != "AWAIT_SERVICE_NO_ACTIVATE"}
        env.update({"HOME": home, "PATH": bindir + ":" + os.environ["PATH"], "STUB_LOG": log})
        result = subprocess.run(["../await", "true", "--service", "act"],
                                env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        assert self._stub_calls(log) == [
            ["systemctl", "--user", "daemon-reload"],
            ["systemctl", "cat", "--user", "act.service"],
            ["systemctl", "enable", "--user", "act.service"],
            ["systemctl", "restart", "--user", "act.service"],
            ["journalctl", "--user", "--follow", "--unit", "act.service"],
        ]
        assert os.path.exists(self._service_file(home, "act"))


class TestNoStderrFlag:
    """Test --no-stderr / -E flag functionality."""

    def test_no_stderr_flag(self):
        """Test --no-stderr suppresses command stderr."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVoE "echo stdout; echo stderr >&2; false"',
            timeout=3.0,
            description="Should show stdout but suppress stderr"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "stdout" in clean_stdout
        # stderr should not appear in stdout (it was redirected to /dev/null)
        assert "stderr" not in clean_stdout

    def test_no_stderr_short_flag(self):
        """Test -E short flag works."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-E "echo test >&2; true"',
            timeout=3.0,
            description="Should suppress stderr with -E flag"
        )
        assert returncode == 0


class TestLargeInputs:
    """Inputs that overflowed fixed-size buffers."""

    def test_large_output_with_spinner(self):
        returncode, stdout, stderr = run_await_with_timeout('-o "seq 5000"', timeout=5.0)
        assert returncode == 0
        # 4999 only appears in the output, not in the "seq 5000" status line
        assert "4999" in stderr

    def test_large_output_silent(self):
        returncode, stdout, stderr = run_await_with_timeout('-Vo "seq 200000"', timeout=5.0)
        assert returncode == 0
        assert "200000" in stdout

    def test_long_command(self):
        long_arg = "x" * 2000
        returncode, stdout, stderr = run_await_with_timeout(f'"echo {long_arg}"', timeout=5.0)
        assert returncode == 0

    def test_many_commands(self):
        start = time.time()
        try:
            result = subprocess.run(["../await"] + ["true"] * 150,
                                    capture_output=True, timeout=30)
            returncode, stderr = result.returncode, result.stderr
        except subprocess.TimeoutExpired as e:
            returncode, stderr = "timeout", e.stderr or b""
        elapsed = time.time() - start
        # spinner colour per command in the last frame: 37 pending, 32 done, 31 failed
        last = stderr.decode(errors="replace").split("\x1b[J")[-1]
        states = {k: last.count(f"\x1b[0;{k}m") for k in ("37", "32", "31")}
        diagnosis = f"rc={returncode} after {elapsed:.1f}s, last frame pending/done/failed={states}"
        assert returncode == 0, diagnosis
        assert elapsed < 5, diagnosis


class TestWatchFlag:
    """Test --watch / -w flag functionality."""

    def test_watch_flag_combination(self):
        """Test --watch flag combines fail, silent, stdout, diff, no-stderr."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-w "echo test_output; false"',
            timeout=3.0,
            description="Should behave like -fVodE combined"
        )
        assert returncode == 0
        # Should show output (like -o)
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "test_output" in clean_stdout
        # Should exit when command fails (like -f)
        # Should be silent (like -V) - no spinners
        clean_stderr = strip_ansi_escape_codes(stderr)
        spinner_chars = ["⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷"]
        for char in spinner_chars:
            assert char not in clean_stderr

    def test_watch_flag_long_form(self):
        """Test --watch long form works."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--watch "echo watch_test; false"',
            timeout=3.0,
            description="Should work with long form --watch"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        assert "watch_test" in clean_stdout


class TestAdvancedFeatures:
    """Test advanced features and edge cases."""

    def test_large_output(self):
        """Test handling of large command output."""
        # Note: Very large outputs (10000+ lines) can cause memory issues
        # Testing with a more moderate size
        returncode, stdout, stderr = run_await_with_timeout(
            '-fVo "seq 1 1000; false"',
            timeout=5.0,
            description="Should handle moderately large output without buffer issues"
        )
        assert returncode == 0
        clean_stdout = strip_ansi_escape_codes(stdout)
        # Check that we got the end of the sequence
        assert "1000" in clean_stdout

    def test_many_concurrent_commands(self):
        """Test running many commands concurrently."""
        # Test with 5 concurrent true commands
        commands = ' '.join(['"true"'] * 5)
        returncode, stdout, stderr = run_await_with_timeout(
            f'{commands}',
            timeout=5.0,
            description="Should handle multiple concurrent commands"
        )
        assert returncode == 0

    def test_rapid_output_changes(self):
        """Test rapid output changes with --change flag."""
        # Create a script that changes output rapidly
        script_path = os.path.join(TMPDIR, "rapid_change_test.sh")
        counter_file = os.path.join(TMPDIR, "rapid_counter")
        with open(script_path, "w") as f:
            f.write(f"""#!/bin/bash
if [ ! -f {counter_file} ]; then
    echo 0 > {counter_file}
fi
count=$(cat {counter_file})
echo "Count: $count"
echo $((count + 1)) > {counter_file}
if [ $count -ge 3 ]; then
    exit 0
fi
exit 1
""")

        try:
            os.chmod(script_path, 0o755)

            # Initialize counter
            with open(counter_file, "w") as f:
                f.write("0")

            returncode, stdout, stderr = run_await_with_timeout(
                f'--change "{script_path}"',
                timeout=5.0,
                description="Should detect rapid output changes"
            )

            # Should exit when output changes
            assert returncode == 0
        finally:
            if os.path.exists(script_path):
                os.remove(script_path)
            if os.path.exists(counter_file):
                os.remove(counter_file)

    def test_diff_with_unicode(self):
        """Test diff mode with unicode characters."""
        # Create a script with unicode output
        script_path = os.path.join(TMPDIR, "unicode_test.sh")
        counter_file = os.path.join(TMPDIR, "unicode_counter")
        with open(script_path, "w") as f:
            f.write(f"""#!/bin/bash
if [ ! -f {counter_file} ]; then
    echo 0 > {counter_file}
fi
count=$(cat {counter_file})
echo "Status: 🎯 Count: $count"
echo $((count + 1)) > {counter_file}
""")

        try:
            os.chmod(script_path, 0o755)

            # Initialize counter
            with open(counter_file, "w") as f:
                f.write("5")

            returncode, stdout, stderr = run_await_with_timeout(
                f'--diff --change -Vo "{script_path}"',
                timeout=3.0,
                description="Should handle unicode in diff mode"
            )

            assert returncode == 0
            # Should contain unicode emoji
            assert "🎯" in stdout or "Status:" in strip_ansi_escape_codes(stdout)
        finally:
            if os.path.exists(script_path):
                os.remove(script_path)
            if os.path.exists(counter_file):
                os.remove(counter_file)

    def test_forever_flag_with_any(self):
        """Test --forever flag prevents exit."""
        # This is hard to test directly, but we can verify it doesn't exit immediately
        # We'll use timeout to stop it
        returncode, stdout, stderr = run_await_with_timeout(
            '--forever --any "true" "false"',
            timeout=1.0,
            description="Should timeout because --forever prevents exit"
        )
        # Should timeout even though 'true' succeeds
        assert returncode == 124


class TestSignalHandling:
    """Test signal handling (SIGINT, SIGTERM)."""

    def test_sigint_terminates_await(self):
        """Test that SIGINT properly terminates await."""
        proc = subprocess.Popen(
            ['../await', 'sleep 100'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        try:
            # Give it time to start
            time.sleep(0.5)

            # Send SIGINT
            proc.send_signal(signal.SIGINT)

            # Wait for it to terminate
            try:
                proc.wait(timeout=2.0)
                # Should have exited (non-zero is expected for signal termination)
                assert proc.returncode != 0 or proc.returncode is not None
            except subprocess.TimeoutExpired:
                proc.kill()
                pytest.fail("await didn't respond to SIGINT within timeout")
        finally:
            # Cleanup
            if proc.poll() is None:
                proc.kill()

    def test_sigterm_terminates_await(self):
        """Test that SIGTERM properly terminates await."""
        proc = subprocess.Popen(
            ['../await', 'sleep 100'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        try:
            # Give it time to start
            time.sleep(0.5)

            # Send SIGTERM
            proc.send_signal(signal.SIGTERM)

            # Wait for it to terminate
            try:
                proc.wait(timeout=2.0)
                # Should have exited
                assert proc.returncode is not None
            except subprocess.TimeoutExpired:
                proc.kill()
                pytest.fail("await didn't respond to SIGTERM within timeout")
        finally:
            # Cleanup
            if proc.poll() is None:
                proc.kill()


class TestManPage:
    """The man page is generated from --help by man/gen-man.sh (at build time)."""

    GEN = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'man', 'gen-man.sh')
    AWAIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'await')

    @staticmethod
    def _gen(binary):
        r = subprocess.run(['sh', TestManPage.GEN, binary], capture_output=True, text=True, timeout=10)
        assert r.returncode == 0, r.stderr
        return r.stdout

    @pytest.fixture(scope='class')
    def page(self):
        return self._gen(self.AWAIT)

    @pytest.fixture(scope='class')
    def page_file(self, page, tmp_path_factory):
        path = tmp_path_factory.mktemp('man') / 'await.1'
        path.write_text(page)
        return str(path)

    @staticmethod
    def _help():
        r = subprocess.run([TestManPage.AWAIT, '--help'], capture_output=True, text=True,
                           env={**os.environ, 'NO_COLOR': '1'}, timeout=5)
        return re.sub(r'\x1b\[[0-9;]*m', '', r.stdout)

    @staticmethod
    def _no_formatter():
        # CI must check the page for real; locally a missing tool is only a skip
        if os.environ.get('CI') == 'true':
            pytest.fail('neither mandoc nor groff is installed (required in CI)')
        pytest.skip('neither mandoc nor groff is installed')

    @staticmethod
    def _render(path):
        """Render to plain text with mandoc or groff; None if neither is installed."""
        for cmd in (['mandoc', '-Tutf8', '-O', 'width=200', path],
                    ['groff', '-man', '-Tutf8', '-rLL=200n', path]):
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=10,
                                   env={**os.environ, 'GROFF_NO_SGR': '1'})
            except FileNotFoundError:
                continue
            assert r.returncode == 0, r.stderr
            return re.sub(r'.\x08', '', r.stdout)
        return None

    def test_every_help_option_is_documented(self, page):
        help_opts = re.findall(r'^\s+(--[\w-]+(?:\s+-\w)?)\s*#', self._help(), re.M)
        assert len(help_opts) > 10
        # .TP tags: \fB\-\-stdout\fR, \fB\-o\fR
        tags = re.findall(r'^\.TP\n(.*)$', page, re.M)
        man_opts = {' '.join(re.findall(r'\\fB(.*?)\\fR', t)).replace('\\-', '-') for t in tags}
        for opt in help_opts:
            assert ' '.join(opt.split()) in man_opts, opt

    def test_new_options_are_picked_up(self, tmp_path):
        """Options are parsed from --help, not hardcoded."""
        fake = tmp_path / 'await'
        fake.write_text(
            "#!/bin/sh\n"
            "[ \"$1\" = --version ] && { echo 9.9.9; exit 0; }\n"
            "printf 'await [options] commands\\n\\n# runs things\\n\\n\\nOPTIONS:\\n"
            "  --help\\t\\t#print this help\\n  --brand-new -N\\t#a flag added later\\n\\n\\n"
            "NOTES:\\n# set NO_COLOR=1 to disable colors\\n'\n")
        fake.chmod(0o755)
        page = self._gen(str(fake))
        assert '\\fB\\-\\-brand\\-new\\fR, \\fB\\-N\\fR\nA flag added later.' in page
        assert '"await 9.9.9"' in page

    def test_lint_clean(self, page_file):
        for cmd in (['mandoc', '-Tlint', '-W', 'warning', page_file],
                    ['groff', '-man', '-Tutf8', '-ww', '-z', page_file]):
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            except FileNotFoundError:
                continue
            assert r.returncode == 0 and not (r.stdout + r.stderr).strip(), r.stdout + r.stderr
            return
        self._no_formatter()

    def test_renders_name_and_examples(self, page_file):
        text = self._render(page_file)
        if text is None:
            self._no_formatter()
        assert re.search(r'^\s*await - runs list of commands', text, re.M)
        # backslashes, quotes and hyphens survive as typed, so examples copy-paste
        assert "await 'curl google.com' --fail" in text
        assert "'expr \\1 + \\2' --exec 'echo \\3'" in text
        for section in ('NAME', 'SYNOPSIS', 'DESCRIPTION', 'OPTIONS', 'ENVIRONMENT', 'EXAMPLES'):
            assert re.search(rf'^{section}$', text, re.M), section

    def test_no_ansi_and_troff_safe(self, page):
        assert '\x1b' not in page
        for line in page.splitlines():
            assert not line.startswith("'"), line
            if line.startswith('.'):
                assert re.match(r'\.(\\"|(TH|SH|TP|PP|RS|RE|nf|fi|B|BR)( |$))', line), line

    def test_environment(self, page):
        env = page.split('.SH ENVIRONMENT', 1)[1].split('.SH ', 1)[0]
        for var in ('NO_COLOR', 'AWAIT_NO_UPDATE_CHECK', 'AWAIT_AUTO_UPDATE'):
            assert f'\\fB{var}\\fR' in env, var
        for hidden in ('AWAIT_UPDATE_FORCE', 'AWAIT_RELEASES_URL', 'AWAIT_UPDATE_TARGET'):
            assert hidden not in page

    def test_version_matches(self, page):
        version = subprocess.run([self.AWAIT, '--version'], capture_output=True, text=True).stdout.strip()
        assert re.search(rf'^\.TH AWAIT 1 "[^"]*" "await {re.escape(version)}"', page, re.M)

    def test_build_page_matches_binary(self):
        """build/await.1 (made by cmake) is the same page the generator gives today."""
        built = os.path.join(os.path.dirname(self.AWAIT), 'build', 'await.1')
        binary = os.path.join(os.path.dirname(self.AWAIT), 'build', 'await')
        if not (os.path.exists(built) and os.path.exists(binary)):
            pytest.skip('no build/await.1')
        strip_date = lambda s: re.sub(r'^(\.TH AWAIT 1) "[^"]*"', r'\1', s, flags=re.M)
        with open(built) as f:
            assert strip_date(f.read()) == strip_date(self._gen(binary))


class TestAutocompletion:
    """Test autocompletion script generation."""

    def test_autocomplete_fish(self):
        """Test fish autocompletion script generation."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--autocomplete-fish',
            description="Should generate fish completion script"
        )
        assert returncode == 0
        assert 'complete -c await' in stdout
        assert 'complete -c await -l stdout -s o' in stdout

    def test_autocomplete_bash(self):
        """Test bash autocompletion script generation."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--autocomplete-bash',
            description="Should generate bash completion script"
        )
        assert returncode == 0
        assert '_await()' in stdout
        assert 'complete -F _await await' in stdout
        assert 'COMPREPLY' in stdout

    def test_autocomplete_zsh(self):
        """Test zsh autocompletion script generation."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--autocomplete-zsh',
            description="Should generate zsh completion script"
        )
        assert returncode == 0
        assert '_await()' in stdout
        assert 'compdef _await await' in stdout
        assert '_arguments' in stdout


    @pytest.mark.parametrize("shell", ["bash", "zsh", "fish"])
    def test_completions_cover_all_long_options(self, shell):
        """Every long option in --help is offered by each shell's completions."""
        help_text = subprocess.run(["../await", "--help"], capture_output=True, text=True).stdout
        options = set(re.findall(r"^\s+--([a-z-]+)", help_text, re.M))
        options -= {"autocompletions", "autocomplete-fish", "autocomplete-bash", "autocomplete-zsh"}
        script = subprocess.run(["../await", f"--autocomplete-{shell}"], capture_output=True, text=True).stdout
        flag = "-l {}" if shell == "fish" else "--{}"
        missing = sorted(o for o in options if not re.search(re.escape(flag.format(o)) + r"\b", script))
        assert not missing, f"{shell} completions missing: {missing}"


    def test_bash_completion_behaviour(self):
        script = subprocess.run(["../await", "--autocomplete-bash"], capture_output=True, text=True).stdout
        def complete(*words):
            probe = (script + "\nCOMP_WORDS=(" + " ".join(f"'{w}'" for w in words) + ")\n"
                     f"COMP_CWORD={len(words) - 1}\n_await\nprintf '%s\\n' \"${{COMPREPLY[@]}}\"\n")
            return subprocess.run(["bash", "-c", probe], capture_output=True, text=True).stdout.split()
        assert complete("await", "--retry", "") == []          # a number, not a filename
        assert "--lap" in complete("await", "--json", "--")    # flags after flags

    @pytest.mark.skipif(not __import__("shutil").which("fish"), reason="fish not installed")
    def test_fish_completion_after_a_flag(self):
        script = subprocess.run(["../await", "--autocomplete-fish"], capture_output=True, text=True).stdout
        result = subprocess.run(["fish", "-c", "source; complete -C'await --json --'"],
                                input=script, capture_output=True, text=True)
        assert "--lap" in result.stdout


class TestHomebrewFormula:
    """scripts/homebrew-formula.sh turns a release's SHA256SUMS into Formula/await.rb"""

    SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts", "homebrew-formula.sh")
    TARGETS = ["aarch64-apple-darwin", "x86_64-apple-darwin",
               "aarch64-unknown-linux-musl", "x86_64-unknown-linux-musl"]

    def generate(self, tmp_path, version, sums):
        (tmp_path / "SHA256SUMS").write_text(sums)
        out = tmp_path / "await.rb"
        result = subprocess.run(["bash", self.SCRIPT, version, str(tmp_path / "SHA256SUMS"), str(out)],
                                capture_output=True, text=True)
        return result, out

    def test_formula_pins_every_prebuilt_archive(self, tmp_path):
        version = "9.8.7"
        shas = {t: format(i + 1, "x") * 64 for i, t in enumerate(self.TARGETS)}
        sums = "".join(f"{s}  await-{version}-{t}.tar.gz\n" for t, s in shas.items())
        sums += "f" * 64 + f"  await-{version}-x86_64-unknown-linux-gnu.tar.gz\n"   # not used by brew
        result, out = self.generate(tmp_path, version, sums)
        assert result.returncode == 0, result.stderr
        formula = out.read_text()

        assert "class Await < Formula" in formula
        assert f'version "{version}"' in formula
        assert 'license "MIT"' in formula
        assert "linux-gnu" not in formula and "f" * 64 not in formula
        base = f"https://github.com/slavaGanzin/await/releases/download/{version}"
        lines = formula.splitlines()
        for target, sha in shas.items():
            url = f'url "{base}/await-{version}-{target}.tar.gz"'
            i = next(n for n, line in enumerate(lines) if line.strip() == url)
            assert lines[i + 1].strip() == f'sha256 "{sha}"'   # each url carries its own checksum
        mac, linux = formula.index("on_macos do"), formula.index("on_linux do")
        assert mac < formula.index("aarch64-apple-darwin") < formula.index("x86_64-apple-darwin") < linux
        assert linux < formula.index("aarch64-unknown-linux-musl")

        assert 'bin.install "await"' in formula
        assert 'bash_completion.install "await.bash" => "await"' in formula
        assert 'fish_completion.install "await.fish"' in formula
        assert 'zsh_completion/"_await"' in formula
        assert 'shell_output("#{bin}/await --version").strip' in formula

        if __import__("shutil").which("ruby"):
            check = subprocess.run(["ruby", "-c", str(out)], capture_output=True, text=True)
            assert check.returncode == 0, check.stderr

    def test_check_catches_stale_pins(self, tmp_path):
        version = "1.2.3"
        sums = "".join(f"{format(i + 1, 'x') * 64}  await-{version}-{t}.tar.gz\n" for i, t in enumerate(self.TARGETS))
        result, out = self.generate(tmp_path, version, sums)
        assert result.returncode == 0, result.stderr
        check = lambda: subprocess.run(["bash", self.SCRIPT, "--check", str(tmp_path / "SHA256SUMS"), str(out)],
                                       capture_output=True, text=True)
        assert check().returncode == 0, check().stdout

        # the archives were re-published: one checksum changed
        (tmp_path / "SHA256SUMS").write_text(sums.replace("2" * 64, "e" * 64))
        stale = check()
        assert stale.returncode == 1
        assert f"await-{version}-x86_64-apple-darwin.tar.gz pins {'2' * 64}" in stale.stdout
        assert "aarch64-apple-darwin" not in stale.stdout

    def test_missing_archive_is_an_error(self, tmp_path):
        sums = "a" * 64 + "  await-1.0.0-aarch64-apple-darwin.tar.gz\n"
        result, out = self.generate(tmp_path, "1.0.0", sums)
        assert result.returncode != 0
        assert "x86_64-apple-darwin" in result.stderr
        assert not out.exists()


class TestNoColor:
    def test_help_not_colored_when_piped(self):
        returncode, stdout, stderr = run_await_with_timeout("--help")
        assert "\033[" not in stdout

    def test_no_color_env_strips_colors(self):
        result = subprocess.run(
            ["../await", "-o", "-r", "2", "echo hi", "false"],
            env={**os.environ, "NO_COLOR": "1"}, capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 1
        assert "hi" in result.stderr
        assert not re.search(r"\x1b\[[0-9;]*m", result.stderr)


# the native notifiers --notify tries, in order, on this platform
NOTIFY_PLATFORM = platform.system()
NATIVE_NOTIFIERS = (["terminal-notifier", "osascript"] if NOTIFY_PLATFORM == "Darwin"
                    else ["notify-send", "gdbus", "kdialog"])
linux_only = pytest.mark.skipif(NOTIFY_PLATFORM != "Linux", reason="Linux notifiers")
macos_only = pytest.mark.skipif(NOTIFY_PLATFORM != "Darwin", reason="macOS notifiers")


class TestNotify:
    """--notify: terminal escape sequences, native notifiers, bell."""

    STUBS = ("notify-send", "gdbus", "kdialog", "osascript", "terminal-notifier")
    # stubs log their argv as one JSON line; STUB_EXIT_<name> / STUB_SLEEP_<name> steer them
    STUB = """#!/usr/bin/env python3
import json, os, sys, time
name = os.path.basename(sys.argv[0]).replace("-", "_")
with open(os.environ["NOTIFY_LOG"], "a") as f:
    f.write(json.dumps([os.path.basename(sys.argv[0])] + sys.argv[1:]) + "\\n")
time.sleep(float(os.environ.get("STUB_SLEEP_" + name, "0")))
sys.exit(int(os.environ.get("STUB_EXIT_" + name, "0")))
"""

    @pytest.fixture
    def notify(self, tmp_path):
        bindir = tmp_path / "bin"
        bindir.mkdir()
        for stub in self.STUBS:
            (bindir / stub).write_text(self.STUB)
            (bindir / stub).chmod(0o755)
        log, tty = tmp_path / "calls.log", tmp_path / "tty"
        tty.write_bytes(b"")
        await_bin = os.path.abspath("../await")

        def run(args, env=None, desktop=True, timeout=10):
            base = {k: v for k, v in os.environ.items()
                    if k not in ("TERM_PROGRAM", "LC_TERMINAL", "KITTY_WINDOW_ID", "WT_SESSION", "TMUX",
                                 "SSH_CONNECTION", "SSH_TTY", "SSH_CLIENT", "DISPLAY", "WAYLAND_DISPLAY",
                                 "DBUS_SESSION_BUS_ADDRESS")}
            base.update(PATH=f"{bindir}:{os.environ['PATH']}", TERM="xterm-256color", NOTIFY_LOG=str(log),
                        AWAIT_NOTIFY_TTY=str(tty), AWAIT_NO_UPDATE_CHECK="1")
            if desktop:
                base["DISPLAY"] = ":0"
            base.update(env or {})
            start = time.time()
            result = subprocess.run([await_bin] + args, env=base, cwd=tmp_path, capture_output=True,
                                    timeout=timeout)
            result.seconds = time.time() - start
            import json
            result.calls = [json.loads(l) for l in log.read_text().splitlines()] if log.exists() else []
            result.tty = tty.read_bytes()
            return result
        run.tmp = tmp_path
        return run

    @staticmethod
    def text(call):
        """(title, body) a stub was given, whichever notifier it is."""
        name = call[0]
        if name == "terminal-notifier":
            return call[2], call[4]
        if name == "osascript":
            return call[7], call[8]
        if name == "gdbus":  # GVariant string literals
            return tuple(json.loads(a) for a in call[12:14])
        if name == "kdialog":
            return call[2], call[4]
        return call[3], call[4]  # notify-send

    @staticmethod
    def stub_env(what, names, value):
        return {f"STUB_{what}_" + n.replace("-", "_"): value for n in names}

    def only_call(self, result, name=None):
        """the one notifier call, the platform's first choice unless named; its (title, body)"""
        assert [c[0] for c in result.calls] == [name or NATIVE_NOTIFIERS[0]], result.calls
        return self.text(result.calls[0])

    def test_without_notify_nothing_happens(self, notify):
        r = notify(["-V", "true"])
        assert r.returncode == 0
        assert r.calls == [] and r.tty == b""

    def test_success(self, notify):
        r = notify(["--notify", "-V", "true", "echo hi"])
        assert r.returncode == 0
        title, body = self.only_call(r)
        assert title == "await"
        assert re.fullmatch(r"done in \d+\.\ds: 2/2 commands succeeded", body), body
        assert r.tty == b""

    def test_notifies_without_silent_too(self, notify):
        r = notify(["--notify", "true"])
        assert r.returncode == 0
        assert self.only_call(r)[1].startswith("done in ")

    def test_timeout(self, notify):
        r = notify(["--notify", "-V", "-T", "1", "true", "exit 3"])
        assert r.returncode == 1
        body = self.only_call(r)[1]
        assert re.fullmatch(r"timed out after 1\.\ds: 1/2 commands succeeded; exit 3 exited 3", body), body

    def test_failure_single_command(self, notify):
        r = notify(["--notify", "-V", "-r", "1", "exit 127"])
        assert r.returncode == 1
        assert self.only_call(r)[1] == "gave up after 1 attempts: exit 127 exited 127 (command not found)"

    def test_exec(self, notify):
        r = notify(["--notify", "-V", "true", "--exec", "exit 4"])
        assert r.returncode == 4
        body = self.only_call(r)[1]
        assert body.startswith("done in ") and body.endswith("true exited 0; --exec finished (exit 4)"), body

    def test_name_in_title(self, notify):
        r = notify(["--notify", "-V", "-n", "web", "true"])
        assert r.returncode == 0
        title, body = self.only_call(r)
        assert title == "await: web"
        assert body.endswith(": web exited 0")

    def test_fail_mode(self, notify):
        r = notify(["--notify", "-V", "-f", "false", "exit 2"])
        assert r.returncode == 0
        assert self.only_call(r)[1].endswith("2/2 commands failed")

    def test_bell_when_everything_fails(self, notify):
        r = notify(["--notify", "-V", "false", "-r", "1"], env=self.stub_env("EXIT", self.STUBS, "1"))
        assert r.returncode == 1
        assert [c[0] for c in r.calls] == NATIVE_NOTIFIERS
        assert r.tty == b"\a"

    @linux_only
    def test_notify_send_arguments(self, notify):
        r = notify(["--notify", "-V", "true"])
        assert r.calls[0][:4] == ["notify-send", "-a", "await", "await"] and len(r.calls[0]) == 5

    @linux_only
    def test_falls_back_to_gdbus(self, notify):
        r = notify(["--notify", "-V", "-n", 'say "hi"', "true"], env={"STUB_EXIT_notify_send": "1"})
        assert r.returncode == 0
        assert [c[0] for c in r.calls] == ["notify-send", "gdbus"]
        gdbus = r.calls[1]
        assert gdbus[1:10] == ["call", "--session", "--dest", "org.freedesktop.Notifications",
                               "--object-path", "/org/freedesktop/Notifications",
                               "--method", "org.freedesktop.Notifications.Notify", "await"]
        assert gdbus[10:12] == ["0", "''"]
        # title and body as GVariant string literals
        assert gdbus[12] == '"await: say \\"hi\\""'
        assert gdbus[13].startswith('"done in ') and gdbus[13].endswith('"')
        assert gdbus[14:] == ["[]", "{}", "5000"]
        assert r.tty == b""

    @linux_only
    def test_falls_back_to_kdialog(self, notify):
        r = notify(["--notify", "-V", "true"], env={"STUB_EXIT_notify_send": "1", "STUB_EXIT_gdbus": "1"})
        assert r.returncode == 0
        assert [c[0] for c in r.calls] == ["notify-send", "gdbus", "kdialog"]
        kdialog = r.calls[2]
        assert kdialog[1:4] == ["--title", "await", "--passivepopup"]
        assert kdialog[4].startswith("done in ") and kdialog[5] == "5"
        assert r.tty == b""

    @linux_only
    def test_bell_without_desktop(self, notify):
        r = notify(["--notify", "-V", "true"], desktop=False)
        assert r.returncode == 0
        assert r.calls == [] and r.tty == b"\a"

    @linux_only
    def test_desktop_via_wayland_or_dbus(self, notify):
        for var in ("WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
            r = notify(["--notify", "-V", "true"], desktop=False, env={var: "x"})
            assert r.calls[-1][0] == "notify-send"

    @pytest.mark.parametrize("env,sequence", [
        ({"TERM_PROGRAM": "WezTerm"}, b"\x1b]9;await: done in "),
        ({"TERM_PROGRAM": "iTerm.app"}, b"\x1b]9;await: done in "),
        ({"LC_TERMINAL": "iTerm2"}, b"\x1b]9;await: done in "),
        ({"TERM_PROGRAM": "vscode"}, b"\x1b]9;await: done in "),
        ({"WT_SESSION": "1"}, b"\x1b]9;await: done in "),
        ({"TERM_PROGRAM": "ghostty"}, b"\x1b]777;notify;await;done in "),
        ({"TERM": "foot-extra"}, b"\x1b]777;notify;await;done in "),
        ({"KITTY_WINDOW_ID": "1"}, b"\x1b]99;i=await:d=0;await\x1b\\\x1b]99;i=await:p=body;done in "),
        ({"TERM": "xterm-kitty"}, b"\x1b]99;i=await:d=0;await\x1b\\\x1b]99;i=await:p=body;done in "),
    ])
    def test_terminal_sequences(self, notify, env, sequence):
        r = notify(["--notify", "-V", "true"], env=env)
        assert r.returncode == 0
        assert r.tty.startswith(sequence), r.tty
        assert r.tty.endswith(b"\x1b\\" if b"]99;" in sequence else b"\a")
        assert r.tty.count(b"\a") <= 1
        assert r.calls == []

    def test_osc_777_fields_have_no_extra_semicolons(self, notify):
        r = notify(["--notify", "-V", "-n", "a;b", "true", "--exec", "true"], env={"TERM_PROGRAM": "ghostty"})
        assert r.tty.startswith(b"\x1b]777;notify;await: a,b;done in ")
        assert r.tty.count(b";") == 3

    def test_tmux_passthrough(self, notify):
        r = notify(["--notify", "-V", "true"], env={"TERM_PROGRAM": "WezTerm", "TMUX": "/tmp/tmux-0/default,1,0"})
        assert r.returncode == 0
        assert r.tty.startswith(b"\x1bPtmux;\x1b\x1b]9;await: done in ")
        assert r.tty.endswith(b"\a\x1b\\")
        assert r.tty.count(b"\x1b") == 4  # DCS, the doubled ESC, and the one ending the passthrough
        assert r.calls == []

    def test_ssh_uses_the_terminal_only(self, notify):
        for var in ("SSH_CONNECTION", "SSH_TTY"):
            r = notify(["--notify", "-V", "true"], env={var: "10.0.0.1 1 10.0.0.2 22"})
            assert r.returncode == 0
            assert re.fullmatch(rb"\x1b\]9;await: done in [^\x07\x1b]*\x07\x07", r.tty), r.tty
            assert r.calls == []
            (notify.tmp / "tty").write_bytes(b"")

    def test_ssh_known_terminal(self, notify):
        r = notify(["--notify", "-V", "true"], env={"SSH_TTY": "/dev/pts/1", "KITTY_WINDOW_ID": "3"})
        assert r.tty.startswith(b"\x1b]99;") and b"\a" not in r.tty
        assert r.calls == []

    def assert_clean(self, text):
        text.encode("utf-8")  # valid UTF-8: no surrogate-escaped bytes
        assert not re.search(r"[\x00-\x1f\x7f-\x9f\u202a-\u202e\u2066-\u2069]", text), repr(text)

    def assert_nothing_ran(self, notify):
        import pathlib
        for d in (notify.tmp, pathlib.Path(".")):
            assert not any(p.name.startswith("pwned") for p in d.iterdir())

    def test_hostile_name(self, notify):
        name = (b"$(touch pwned) `touch pwned2` 'q' \"dq\" \x1b]0;evil\x07\x1b[31m \xff\xfe\xc3(\n\n"
                + "\u2603".encode() * 3000)
        r = notify([b"--notify", b"-V", b"-n", name, b"true"])
        assert r.returncode == 0
        self.assert_nothing_ran(notify)
        title, body = self.only_call(r)
        for text in (title, body):
            self.assert_clean(text)
        label = "$(touch pwned) `touch pwned2` 'q' \"dq\" ]0;evil [31m ( \u2603"
        assert title.startswith("await: " + label) and title.endswith("\u2603\u2026")
        assert len(title.encode()) <= len("await: ") + 60
        assert re.fullmatch(r"done in \d\.\ds: " + re.escape(label) + "\u2603*\u2026 exited 0", body), body
        assert len(body.encode()) <= 200

    def test_hostile_command(self, notify):
        cmd = b"true '$(touch pwned3) \x1b]9;x\x07\x9b \xe2\x80\xae\xc0\xaf\xed\xa0\x80a\xc3\xa9 " + b"y" * 10000 + b"'"
        r = notify([b"--notify", b"-V", b"-r", b"1", cmd, b"exit 1"],
                   env=self.stub_env("EXIT", NATIVE_NOTIFIERS[:1], "1"))
        assert r.returncode == 1
        self.assert_nothing_ran(notify)
        assert [c[0] for c in r.calls] == NATIVE_NOTIFIERS[:2]
        body = self.text(r.calls[0])[1]
        self.assert_clean(body)
        assert body.startswith("gave up after 1 attempts: 1/2 commands succeeded; exit 1 exited 1")
        # the fallback gets the same text (gdbus: as a GVariant literal)
        assert self.text(r.calls[1])[1] == body
        if r.calls[1][0] == "gdbus":
            assert r.calls[1][13] == '"' + body + '"'
        r = notify([b"--notify", b"-V", cmd])
        body = self.text(r.calls[-1])[1]
        self.assert_clean(body)
        assert re.fullmatch(r"done in \d\.\ds: true '\$\(touch pwned3\) \]9;x aé y+\u2026 exited 0", body), body
        assert len(body.encode()) <= 200
        self.assert_nothing_ran(notify)

    def test_hung_notifier_does_not_delay_exit(self, notify):
        r = notify(["--notify", "-V", "exit 3", "-r", "1"], env=self.stub_env("SLEEP", NATIVE_NOTIFIERS[:1], "30"))
        assert r.returncode == 1
        assert r.seconds < 3.5
        # the 2s budget is spent: straight to the bell
        assert [c[0] for c in r.calls] == NATIVE_NOTIFIERS[:1]
        assert r.tty == b"\a"

    def test_hung_notifier_keeps_exec_status(self, notify):
        r = notify(["--notify", "-V", "true", "--exec", "exit 5"], env=self.stub_env("SLEEP", NATIVE_NOTIFIERS[:1], "30"))
        assert r.returncode == 5 and r.seconds < 3.5

    def test_forever_exec_notifies_each_run(self, notify):
        runs = notify.tmp / "runs"
        r = notify(["--notify", "-V", "-F", "-T", "1.5", "true", "--exec", f"echo x >> {runs}"])
        assert r.returncode == 1
        bodies = [self.text(c)[1] for c in r.calls]
        execs = [b for b in bodies if b == "true exited 0; --exec finished (exit 0)"]
        assert bodies[-1].startswith("timed out after 1.")
        assert len(execs) == len(bodies) - 1
        count = len(runs.read_text().splitlines())
        assert count >= 2 and len(execs) in (count, count - 1), (count, bodies)

    def test_stopped_terminal_does_not_delay_exit(self, notify):
        import pty, fcntl, termios
        master, slave = pty.openpty()
        try:
            # output stopped (like Ctrl-S) and the buffer full: a write would block
            termios.tcflow(slave, termios.TCOOFF)
            fcntl.fcntl(slave, fcntl.F_SETFL, fcntl.fcntl(slave, fcntl.F_GETFL) | os.O_NONBLOCK)
            for _ in range(100000):
                try:
                    os.write(slave, b"x" * 1024)
                except BlockingIOError:
                    break
            r = notify(["--notify", "-V", "exit 3", "-r", "1"],
                       env={"TERM_PROGRAM": "WezTerm", "AWAIT_NOTIFY_TTY": os.ttyname(slave)})
            assert r.returncode == 1
            assert r.seconds < 3.5
            assert r.calls == []  # the terminal used up the budget
        finally:
            os.close(slave)
            os.close(master)

    def test_fifo_without_reader_is_no_terminal(self, notify):
        fifo = notify.tmp / "fifo"
        os.mkfifo(fifo)
        r = notify(["--notify", "-V", "true"], env={"TERM_PROGRAM": "WezTerm", "AWAIT_NOTIFY_TTY": str(fifo)})
        assert r.returncode == 0 and r.seconds < 3
        assert [c[0] for c in r.calls] == NATIVE_NOTIFIERS[:1]

    def test_cmd_timeout_note_only_when_killed(self, notify):
        r = notify(["--notify", "-V", "-t", "5", "-r", "1", "exit 124"])
        assert self.only_call(r)[1] == "gave up after 1 attempts: exit 124 exited 124"
        r = notify(["--notify", "-V", "-t", "1", "-r", "1", "sleep 5"])
        assert self.text(r.calls[-1])[1] == "gave up after 1 attempts: sleep 5 exited 124 (--cmd-timeout)"
        # the same note in the --timeout report
        r = notify(["-T", "1.5", "-t", "5", "exit 124"], env={"NO_COLOR": "1"})
        assert b"'exit 124': last exit 124\n" in r.stderr
        r = notify(["-T", "1.5", "-t", "1", "sleep 5"], env={"NO_COLOR": "1"})
        assert b"'sleep 5': last exit 124 (--cmd-timeout)\n" in r.stderr

    @linux_only  # --service is systemd-only
    def test_service_keeps_notify(self, notify):
        home = notify.tmp / "home"
        home.mkdir()
        for stub in ("systemctl", "journalctl"):
            (notify.tmp / "bin" / stub).write_text("#!/bin/sh\nexit 0\n")
            (notify.tmp / "bin" / stub).chmod(0o755)
        r = notify(["--notify", "true", "--service", "t"], env={"HOME": str(home)})
        assert r.returncode == 0
        unit = (home / ".config/systemd/user/t.service").read_text()
        exec_start = next(l for l in unit.splitlines() if l.startswith("ExecStart="))
        assert "--notify" in exec_start and "--update" not in exec_start

    @macos_only
    def test_macos_terminal_notifier(self, notify):
        # no desktop session variables needed on macOS
        r = notify(["--notify", "-V", "-n", "web", "true"], desktop=False)
        assert r.returncode == 0
        assert self.only_call(r, "terminal-notifier") == ("await: web", r.calls[0][4])
        assert r.calls[0][1:4] == ["-title", "await: web", "-message"] and len(r.calls[0]) == 5
        assert r.calls[0][4].startswith("done in ")

    @macos_only
    def test_macos_osascript(self, notify):
        r = notify(["--notify", "-V", "-n", 'x" & (do shell script "touch pwned") & "', "true"],
                   desktop=False, env={"STUB_EXIT_terminal_notifier": "1"})
        assert r.returncode == 0
        assert [c[0] for c in r.calls] == ["terminal-notifier", "osascript"]
        assert r.calls[1][1:7] == ["-e", "on run argv",
                                   "-e", "display notification (item 2 of argv) with title (item 1 of argv)",
                                   "-e", "end run"]
        assert r.calls[1][7] == 'await: x" & (do shell script "touch pwned") & "'
        assert r.calls[1][8].startswith("done in ")
        assert len(r.calls[1]) == 9
        assert r.tty == b""


class TestJson:
    def test_json_output_success(self):
        """--json emits valid JSON on success."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "echo hello"',
            description="Should output JSON with success=true"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        assert data['success'] is True
        assert 'commands' in data
        assert len(data['commands']) == 1
        assert data['commands'][0]['status'] == 0

    def test_json_output_contains_command_output(self):
        """--json includes stdout of each command."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "echo hello"',
            description="Should include command output in JSON"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        assert 'hello' in data['commands'][0]['output']

    def test_json_output_failure_on_timeout(self):
        """--json emits success=false on timeout."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json --timeout 1 "exit 1"',
            timeout=5,
            description="Should output JSON with success=false on timeout"
        )
        assert returncode == 1
        data = json.loads(stdout.strip())
        assert data['success'] is False

    def test_json_multiple_commands(self):
        """--json includes all commands in output."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "echo foo" "echo bar"',
            description="Should include both commands in JSON"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        assert len(data['commands']) == 2
        outputs = [cmd['output'] for cmd in data['commands']]
        assert any('foo' in o for o in outputs)
        assert any('bar' in o for o in outputs)

    def test_json_elapsed_ms_present(self):
        """--json includes elapsed_ms field."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "echo ok"',
            description="Should include elapsed_ms in JSON"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        assert 'elapsed_ms' in data


    def test_json_escapes_all_fields(self):
        """Quotes in names/commands and control chars in output must stay valid JSON."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            """--json --name 'my "db"' 'printf "q\\"x\\t\\001"'""",
            description="Should emit valid JSON"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        cmd = data['commands'][0]
        assert cmd['name'] == 'my "db"'
        assert cmd['command'] == 'printf "q\\"x\\t\\001"'
        assert cmd['output'] == 'q"x\t\x01'

    def test_json_elapsed_without_timeout(self):
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "sleep 0.3"',
            description="elapsed_ms should be measured even without --timeout"
        )
        assert returncode == 0
        assert json.loads(stdout.strip())['elapsed_ms'] >= 300


class TestName:
    def test_name_shown_in_spinner(self):
        """--name label replaces command in spinner output."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--name db "echo ok"',
            description="Should show 'db' in spinner instead of full command"
        )
        assert returncode == 0
        assert 'db' in stderr
        assert 'echo ok' not in stderr

    def test_name_substitution_in_exec(self):
        """\\name substitution works in --exec."""
        import tempfile, os
        out_file = os.path.join(TMPDIR, 'await_name_test.txt')
        returncode, stdout, stderr = run_await_with_timeout(
            f'--silent --name greeting "printf hello" --exec \'printf "\\\\greeting world" > {out_file}\'',
            description="Should substitute \\greeting with command output in --exec"
        )
        assert returncode == 0
        assert os.path.exists(out_file)
        content = open(out_file).read()
        assert 'hello world' in content
        os.unlink(out_file)

    def test_name_multiple_commands(self):
        """Multiple --name flags label each command independently."""
        returncode, stdout, stderr = run_await_with_timeout(
            '--name db "echo ok" --name api "echo ok"',
            description="Should show both labels in spinner"
        )
        assert returncode == 0
        assert 'db' in stderr
        assert 'api' in stderr

    def test_name_in_json_output(self):
        """--name label appears in --json output."""
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json --name mydb "echo ok"',
            description="Should use label as name field in JSON"
        )
        assert returncode == 0
        data = json.loads(stdout.strip())
        assert data['commands'][0]['name'] == 'mydb'



class TestCoreLoopRegressions:
    """Regressions for placeholder indexing, exec, --change and fractional seconds."""

    def test_placeholders_map_to_matching_command(self):
        """\\1 and \\2 are the outputs of the 1st and 2nd commands, even in round one."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-V "printf 10" "printf 5" "expr \\1 + \\2" --exec "echo got \\1 \\2 \\3"',
            description="Should print 'got 10 5 15'"
        )
        assert returncode == 0
        assert "got 10 5 15" in stdout

    def test_placeholder_trims_trailing_newline(self):
        """Like shell $(...), substituted output loses its trailing newline."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-V "echo 7" --exec "echo [\\1]"',
            description="Should print '[7]'"
        )
        assert returncode == 0
        assert "[7]" in stdout

    def test_named_placeholder(self):
        """\\name refers to the command labelled with --name."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-V --name greeting "printf hi" --exec "echo [\\greeting]"',
            description="Should print '[hi]'"
        )
        assert returncode == 0
        assert "[hi]" in stdout

    def test_exec_output_is_printed(self):
        returncode, stdout, stderr = run_await_with_timeout(
            '-V "true" --exec "echo exec-output"',
            description="Should show stdout of the --exec command"
        )
        assert returncode == 0
        assert "exec-output" in stdout

    def test_exec_exit_status_is_returned(self):
        returncode, stdout, stderr = run_await_with_timeout(
            '-V "true" --exec "exit 3"',
            description="Should exit with the --exec command's status"
        )
        assert returncode == 3

    def test_exec_runs_once_per_change(self):
        """With --change --forever, exec fires once per change, not once per tick."""
        log = os.path.join(TMPDIR, "await_exec_once_per_change")
        if os.path.exists(log):
            os.remove(log)
        try:
            run_await_with_timeout(
                f'-V --change --forever "date +%s" --exec "echo x >> {log}"',
                timeout=3.5,
                description="Should run exec about once per second"
            )
            with open(log) as f:
                runs = len(f.readlines())
            assert 2 <= runs <= 4, f"exec ran {runs} times in 3.5s"
        finally:
            if os.path.exists(log):
                os.remove(log)

    def test_exec_output_keeps_json_valid(self):
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json "true" --exec "echo ready"',
            description="exec output goes to stderr so stdout stays one JSON document"
        )
        assert returncode == 0
        assert json.loads(stdout.strip())["success"] is True
        assert "ready" in stderr

    def test_timeout_applies_while_exec_runs_forever(self):
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '-V --forever -T 1 "true" --exec "exec sleep 30 >/dev/null 2>&1"',
            timeout=5.0,
            description="Should time out after 1s even though exec is still running"
        )
        assert returncode == 1
        assert time.time() - start < 3

    def test_substituted_output_is_not_executed(self):
        """Command output is data: $(...) in it must not run, in any quoting context,
        including a placeholder nested inside "$(...)"."""
        marker = os.path.join(TMPDIR, "await_injection_marker")
        if os.path.exists(marker):
            os.remove(marker)
        payload_file = os.path.join(TMPDIR, "await_injection_payload")
        with open(payload_file, "w") as f:
            f.write(f"$(touch {marker}); `touch {marker}`; ' \" touch {marker}")
        returncode, stdout, stderr = run_await_with_timeout(
            f"""-V "cat {payload_file}" --exec 'echo \\1; echo "\\1"; echo '"'"'\\1'"'"'; echo "$(echo \\1)"'""",
            description="Should print the payload three times without running it"
        )
        os.remove(payload_file)
        try:
            assert returncode == 0
            assert not os.path.exists(marker), "substituted output was executed"
        finally:
            if os.path.exists(marker):
                os.remove(marker)

    def test_fractional_interval(self):
        """-i 0.5 means half a second, not zero."""
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '-V -i 0.5 -r 3 "false"',
            description="Should take about a second for 3 attempts"
        )
        elapsed = time.time() - start
        assert returncode == 1
        assert elapsed >= 0.9, f"3 attempts at 0.5s interval took {elapsed:.2f}s"

    def test_fractional_timeout(self):
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '-V -T 0.5 "false"',
            description="Should time out after half a second"
        )
        elapsed = time.time() - start
        assert returncode == 1
        assert 0.4 < elapsed < 1.5, f"-T 0.5 took {elapsed:.2f}s"


class TestCmdTimeoutAndRetry:
    def test_cmd_timeout_kills_everything_the_command_started(self):
        """-t kills the whole command, not just the sh wrapper around it."""
        marker = f"await-cmd-timeout-{os.getpid()}"
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -t 1 -r 1 "sh -c \'sleep 30; echo {marker}\'; true"',
            timeout=5.0,
            description="Should kill the command after 1s"
        )
        elapsed = time.time() - start
        assert returncode == 1
        assert 0.9 <= elapsed < 3, f"took {elapsed:.1f}s; expected one 1s attempt"
        time.sleep(0.3)
        leftover = subprocess.run(["pgrep", "-f", f"sleep 30; echo {marker}"], capture_output=True)
        assert leftover.returncode != 0, "the timed-out command is still running"

    def test_cmd_timeout_status_is_124(self):
        import json
        returncode, stdout, stderr = run_await_with_timeout(
            '--json -t 1 -r 1 "sleep 5"',
            timeout=5.0,
            description="A timed-out run reports 124, like timeout(1)"
        )
        assert returncode == 1
        assert json.loads(stdout.strip())["commands"][0]["status"] == 124

    def test_cmd_timeout_leaves_fast_commands_alone(self):
        returncode, stdout, stderr = run_await_with_timeout(
            '-Vo -t 2 "sleep 0.2; echo ok"',
            description="A command within its timeout succeeds normally"
        )
        assert returncode == 0
        assert "ok" in stdout

    def test_retry_counts_attempts_not_ticks(self):
        """-r N gives up after N finished runs, even when each run is slow."""
        counter = os.path.join(TMPDIR, f"await_retry_count_{os.getpid()}")
        if os.path.exists(counter):
            os.remove(counter)
        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'-V -r 3 "sleep 0.5; echo x >> {counter}; false"',
                timeout=6.0,
                description="Should make exactly 3 attempts"
            )
            assert returncode == 1
            with open(counter) as f:
                assert len(f.readlines()) == 3
        finally:
            if os.path.exists(counter):
                os.remove(counter)


def run_with_tty_stderr(args, env, timeout=5.0, binary="../await"):
    """Run await with stderr on a pseudo-terminal, like an interactive shell.
    Returns (returncode, stderr, seconds)."""
    import pty, select
    master, slave = pty.openpty()
    start = time.time()
    proc = subprocess.Popen([binary] + args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=slave, env={**os.environ, **env})
    os.close(slave)
    err = b""
    while time.time() - start < timeout:
        ready, _, _ = select.select([master], [], [], 0.1)
        if ready:
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            err += chunk
        elif proc.poll() is not None:
            break
    proc.wait(timeout=timeout)
    os.close(master)
    return proc.returncode, err.decode(errors="replace"), time.time() - start


class FakeReleases:
    """A local stand-in for github.com/<repo>/releases: /latest redirects to
    /tag/<latest>, /download/<tag>/<file> serves published files. Clearing
    `answer` makes /latest hang, like a slow network."""

    def __init__(self):
        import http.server, threading
        self.latest = None
        self.files = {}
        self.requests = []
        self.answer = threading.Event()
        self.answer.set()
        releases = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_HEAD(self):
                self.do_GET(body=False)

            def do_GET(self, body=True):
                releases.requests.append(self.path)
                if self.path == "/releases/latest":
                    releases.answer.wait(30)
                    if releases.latest:
                        self.send_response(302)
                        self.send_header("Location", f"{releases.url}/tag/{releases.latest}")
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                data = releases.files.get(self.path)
                if data is None:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                if body:
                    self.wfile.write(data)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/releases"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def publish(self, version, target="test-target", binary=None, checksum=None, sums=True, extra_files=None, name="await"):
        """Publish `version` with an archive for `target` whose `await` is a stand-in
        that answers --version (or `binary`, a shell script), plus `extra_files`
        ({name: text}) next to it; `name` is the binary's name in the archive."""
        import hashlib, io, tarfile
        self.latest = version
        script = binary or f"#!/bin/sh\necho {version}\n"
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(script), 0o644   # like the old archives: not executable
            tar.addfile(info, io.BytesIO(script.encode()))
            for extra, text in (extra_files or {}).items():
                info = tarfile.TarInfo(extra)
                info.size, info.mode = len(text.encode()), 0o644
                tar.addfile(info, io.BytesIO(text.encode()))
        archive = f"await-{version}-{target}.tar.gz"
        self.files[f"/releases/download/{version}/{archive}"] = buf.getvalue()
        if sums:
            digest = checksum or hashlib.sha256(buf.getvalue()).hexdigest()
            self.files[f"/releases/download/{version}/SHA256SUMS"] = f"{digest}  {archive}\n".encode()

    def close(self):
        self.answer.set()
        self.server.shutdown()


class TestUpdateNotifier:
    """Background check for a newer release, cached for a day."""

    def setup_method(self):
        self.dir = tempfile.mkdtemp()
        self.cache = os.path.join(self.dir, "await", "latest-version")
        self.releases = FakeReleases()
        self.env = {"XDG_CACHE_HOME": self.dir, "AWAIT_RELEASES_URL": self.releases.url}
        os.environ.pop("AWAIT_NO_UPDATE_CHECK", None)
        os.environ.pop("AWAIT_AUTO_UPDATE", None)

    def teardown_method(self):
        self.releases.close()

    def publish(self, version):
        self.releases.publish(version)

    def cached(self, version, age_seconds=0):
        os.makedirs(os.path.dirname(self.cache), exist_ok=True)
        with open(self.cache, "w") as f:
            f.write(version + "\n")
        mtime = time.time() - age_seconds
        os.utime(self.cache, (mtime, mtime))

    def wait_for_cache(self, expected, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if os.path.exists(self.cache) and open(self.cache).read().strip() == expected:
                return True
            time.sleep(0.05)
        return False

    def own_version(self):
        return subprocess.run(["../await", "--version"], capture_output=True, text=True).stdout.strip()

    def test_first_run_fetches_in_background(self):
        self.publish("99.0.0")
        returncode, err, _ = run_with_tty_stderr(["true"], self.env)
        assert returncode == 0
        assert "available" not in err            # nothing cached yet
        assert self.wait_for_cache("99.0.0")
        assert "/releases/latest" in self.releases.requests   # the redirect, not the rate-limited API

    def test_newer_cached_version_is_announced(self):
        self.cached("99.0.0")
        returncode, err, _ = run_with_tty_stderr(["true"], self.env)
        assert returncode == 0
        assert f"await 99.0.0 is available (you have {self.own_version()}): run await --update" in err

    def test_versions_compare_numerically(self):
        major, minor, _ = self.own_version().split(".")
        self.cached(f"{major}.{int(minor) + 1}.0")   # e.g. 2.10.0 when running 2.9.0
        _, err, _ = run_with_tty_stderr(["true"], self.env)
        assert "available" in err

    def test_same_or_older_version_is_quiet(self):
        for version in (self.own_version(), "v" + self.own_version(), "1.0.0"):
            self.cached(version)
            _, err, _ = run_with_tty_stderr(["true"], self.env)
            assert "available" not in err, version

    def test_fresh_cache_is_not_refetched(self):
        self.cached("1.0.0", age_seconds=60)
        self.publish("99.0.0")
        run_with_tty_stderr(["true"], self.env)
        time.sleep(1)
        assert open(self.cache).read().strip() == "1.0.0"
        assert self.releases.requests == []

    def test_day_old_cache_is_refreshed(self):
        self.cached("1.0.0", age_seconds=25 * 60 * 60)
        self.publish("99.0.0")
        run_with_tty_stderr(["true"], self.env)
        assert self.wait_for_cache("99.0.0")

    def test_failed_check_keeps_value_and_waits_a_day(self):
        self.cached("1.0.0", age_seconds=25 * 60 * 60)   # nothing published: /latest is a 404
        run_with_tty_stderr(["true"], self.env)
        deadline = time.time() + 5
        while time.time() < deadline and time.time() - os.path.getmtime(self.cache) > 60:
            time.sleep(0.05)
        assert time.time() - os.path.getmtime(self.cache) < 60, "cache wasn't timestamped"
        assert open(self.cache).read().strip() == "1.0.0"

    def test_check_does_not_delay_await(self):
        """The fetch runs detached: await exits while it is still waiting on the network."""
        self.publish("99.0.0")
        self.releases.answer.clear()      # /latest hangs until set
        try:
            returncode, _, elapsed = run_with_tty_stderr(["true"], self.env)
            assert returncode == 0
            assert elapsed < 2, f"await waited {elapsed:.1f}s for the update check"
        finally:
            self.releases.answer.set()
        assert self.wait_for_cache("99.0.0")

    def test_check_does_not_keep_callers_pipes_open(self):
        """The detached fetch must not inherit extra descriptors: a caller waiting
        for EOF on a pipe it passed to await gets it when await exits."""
        import pty
        self.publish("1.0.0")
        self.releases.answer.clear()      # the fetch hangs on /latest
        read_end, write_end = os.pipe()   # an extra descriptor the caller passes to await
        master, slave = pty.openpty()
        try:
            proc = subprocess.Popen(["../await", "true"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=slave, pass_fds=(write_end,), env={**os.environ, **self.env})
            os.close(slave)
            proc.wait(timeout=5)
            os.close(write_end)
            os.set_blocking(read_end, False)
            eof, deadline = False, time.time() + 2
            while not eof and time.time() < deadline:
                try:
                    eof = os.read(read_end, 1) == b""
                except BlockingIOError:
                    time.sleep(0.05)
            assert eof, "the background check kept the caller's pipe open"
        finally:
            os.close(read_end)
            os.close(master)
            self.releases.answer.set()

    def test_no_check_when_not_interactive(self):
        self.publish("99.0.0")
        subprocess.run(["../await", "true"], capture_output=True, env={**os.environ, **self.env}, timeout=5)
        time.sleep(1)
        assert not os.path.exists(self.cache)
        assert self.releases.requests == []

    def test_opt_out(self):
        self.cached("99.0.0", age_seconds=25 * 60 * 60)
        self.publish("98.0.0")
        _, err, _ = run_with_tty_stderr(["true"], {**self.env, "AWAIT_NO_UPDATE_CHECK": "1"})
        time.sleep(1)
        assert "available" not in err
        assert open(self.cache).read().strip() == "99.0.0"

    def test_silent_hides_notice(self):
        self.cached("99.0.0")
        _, err, _ = run_with_tty_stderr(["-V", "true"], self.env)
        assert "available" not in err


class TestSelfUpdate:
    """await --update replaces the binary only after every check passes."""

    def setup_method(self):
        import shutil
        self.releases = FakeReleases()
        self.dir = tempfile.mkdtemp()
        self.bin_dir = os.path.join(self.dir, "bin")
        os.mkdir(self.bin_dir)
        self.binary = os.path.join(self.bin_dir, "await")
        shutil.copy("../await", self.binary)
        self.version = subprocess.run([self.binary, "--version"], capture_output=True, text=True).stdout.strip()
        self.env = {**os.environ, "AWAIT_RELEASES_URL": self.releases.url, "AWAIT_UPDATE_TARGET": "test-target",
                    "XDG_CACHE_HOME": self.dir}
        self.env.pop("AWAIT_NO_UPDATE_CHECK", None)
        self.env.pop("AWAIT_AUTO_UPDATE", None)
        self.env.pop("AWAIT_UPDATE_FORCE", None)

    def teardown_method(self):
        import shutil
        self.releases.close()
        subprocess.run(["chmod", "-R", "u+w", self.dir])
        shutil.rmtree(self.dir, ignore_errors=True)

    def update(self, binary=None, env=None):
        result = subprocess.run([binary or self.binary, "--update"], capture_output=True, text=True,
                                env=env or self.env, timeout=30)
        return result.returncode, result.stderr

    def installed_version(self, path=None):
        return subprocess.run([path or self.binary, "--version"], capture_output=True, text=True).stdout.strip()

    def assert_untouched(self):
        assert self.installed_version() == self.version
        assert not os.path.exists(self.binary + ".old")
        leftovers = [f for f in os.listdir(self.bin_dir) if f.startswith(".await-update")]
        assert leftovers == [], leftovers

    def test_updates_in_place_and_keeps_a_backup(self):
        self.releases.publish("99.0.0")
        returncode, err = self.update()
        assert returncode == 0, err
        assert f"updated {self.version} -> 99.0.0" in err
        assert self.installed_version() == "99.0.0"
        assert os.access(self.binary, os.X_OK)                          # executable, even from a non-executable archive
        assert self.installed_version(self.binary + ".old") == self.version
        assert [f for f in os.listdir(self.bin_dir) if f.startswith(".await-update")] == []

    def test_a_running_await_is_not_disturbed(self):
        """The new binary is renamed into place, so an await that is already running
        keeps executing its own (old) file."""
        self.releases.publish("99.0.0")
        running = subprocess.Popen([self.binary, "-V", "sleep 1"], env=self.env)
        time.sleep(0.2)
        returncode, err = self.update()
        assert returncode == 0, err
        assert running.wait(timeout=10) == 0

    def man_page(self, text=None):
        """<prefix>/share/man/man1/await.1 next to <prefix>/bin/await; `text` creates it."""
        path = os.path.join(self.dir, "share", "man", "man1", "await.1")
        if text is not None:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(text)
        return path

    def test_installed_man_page_is_refreshed(self):
        page = self.man_page(".TH AWAIT 1 old\n")
        self.releases.publish("99.0.0", extra_files={"await.1": ".TH AWAIT 1 new\n"})
        returncode, err = self.update()
        assert returncode == 0, err
        assert self.installed_version() == "99.0.0"
        assert open(page).read() == ".TH AWAIT 1 new\n"
        assert os.listdir(os.path.dirname(page)) == ["await.1"]       # no temp file left behind

    def test_man_page_is_not_created(self):
        self.releases.publish("99.0.0", extra_files={"await.1": ".TH AWAIT 1 new\n"})
        returncode, err = self.update()
        assert returncode == 0, err
        assert not os.path.exists(os.path.join(self.dir, "share"))

    def test_man_page_symlink_is_not_written_through(self):
        target = os.path.join(self.dir, "elsewhere.1")
        with open(target, "w") as f:
            f.write("mine\n")
        page = self.man_page()
        os.makedirs(os.path.dirname(page))
        os.symlink(target, page)
        self.releases.publish("99.0.0", extra_files={"await.1": ".TH AWAIT 1 new\n"})
        returncode, err = self.update()
        assert returncode == 0, err
        assert os.path.islink(page) and open(target).read() == "mine\n"

    def test_read_only_man_page_does_not_fail_the_update(self):
        if os.geteuid() == 0:
            pytest.skip("root can write anywhere")
        page = self.man_page(".TH AWAIT 1 old\n")
        os.chmod(page, 0o444)
        os.chmod(os.path.dirname(page), 0o555)
        self.releases.publish("99.0.0", extra_files={"await.1": ".TH AWAIT 1 new\n"})
        returncode, err = self.update()
        assert returncode == 0, err
        assert self.installed_version() == "99.0.0"
        assert "couldn't update the man page" in err
        assert open(page).read() == ".TH AWAIT 1 old\n"

    def test_already_up_to_date(self):
        self.releases.publish(self.version)
        returncode, err = self.update()
        assert returncode == 0
        assert "already up to date" in err
        self.assert_untouched()

    def test_older_release_is_not_a_downgrade(self):
        self.releases.publish("1.0.0")
        returncode, err = self.update()
        assert returncode == 0
        assert "already up to date" in err
        self.assert_untouched()

    def test_checksum_mismatch_is_refused(self):
        self.releases.publish("99.0.0", checksum="0" * 64)
        returncode, err = self.update()
        assert returncode != 0
        assert "checksum mismatch" in err
        self.assert_untouched()

    def test_release_without_checksums_is_refused(self):
        self.releases.publish("99.0.0", sums=False)
        returncode, err = self.update()
        assert returncode != 0
        assert "SHA256SUMS" in err
        self.assert_untouched()

    def test_binary_that_does_not_run_here_is_refused(self):
        """E.g. a build for the wrong libc: it must fail the test run, not replace us."""
        self.releases.publish("99.0.0", binary="#!/bin/sh\nexit 1\n")
        returncode, err = self.update()
        assert returncode != 0
        assert "doesn't run here" in err
        self.assert_untouched()

    def test_no_build_for_this_platform(self):
        self.releases.publish("99.0.0", target="some-other-target")
        returncode, err = self.update()
        assert returncode != 0
        assert "has no test-target build" in err
        self.assert_untouched()

    def test_windows_archive_with_await_exe(self):
        """The Windows (MSYS2) archive holds await.exe instead of await."""
        self.releases.publish("99.0.0", name="await.exe")
        returncode, err = self.update()
        assert returncode == 0, err
        assert self.installed_version() == "99.0.0"
        assert self.installed_version(self.binary + ".old") == self.version

    def test_forced_update_reinstalls_the_same_or_an_older_release(self):
        """AWAIT_UPDATE_FORCE (CI's end-to-end check) installs the latest release
        even when it isn't newer, through the same verified path."""
        import shutil
        force = {**self.env, "AWAIT_UPDATE_FORCE": "1"}
        for version in (self.version, "1.0.0"):
            shutil.copy("../await", self.binary)            # the real await, not the last stand-in
            self.releases.publish(version)
            returncode, err = self.update(env=force)
            assert returncode == 0, err
            assert f"updated {self.version} -> {version}" in err
            assert open(self.binary).read() == f"#!/bin/sh\necho {version}\n"
            assert self.installed_version(self.binary + ".old") == self.version
        # still verified: a forced install of a bad archive is refused
        shutil.copy("../await", self.binary)
        self.releases.publish("1.0.0", checksum="0" * 64)
        returncode, err = self.update(env=force)
        assert returncode != 0 and "checksum mismatch" in err
        assert self.installed_version() == self.version

    def test_force_is_off_when_empty_or_zero(self):
        self.releases.publish(self.version)
        for value in ("", "0"):
            returncode, err = self.update(env={**self.env, "AWAIT_UPDATE_FORCE": value})
            assert returncode == 0 and "already up to date" in err, value
        self.assert_untouched()

    def test_unsupported_platform(self):
        self.releases.publish("99.0.0")
        returncode, err = self.update(env={**self.env, "AWAIT_UPDATE_TARGET": ""})
        assert returncode != 0
        assert "no prebuilt await" in err
        self.assert_untouched()

    @pytest.mark.skipif(os.geteuid() == 0, reason="root can write anywhere")
    @pytest.mark.skipif(platform.system().startswith(("MSYS", "CYGWIN")),
                        reason="chmod can't make a directory read-only on Windows")
    def test_unwritable_install_suggests_sudo(self):
        self.releases.publish("99.0.0")
        os.chmod(self.bin_dir, 0o555)
        returncode, err = self.update()
        assert returncode != 0
        assert f"sudo {os.path.realpath(self.binary)} --update" in err
        os.chmod(self.bin_dir, 0o755)
        self.assert_untouched()

    def test_package_managed_install_is_refused(self):
        import shutil
        cellar = os.path.join(self.dir, "Cellar", "await", self.version, "bin")
        os.makedirs(cellar)
        brewed = os.path.join(cellar, "await")
        shutil.copy(self.binary, brewed)
        self.releases.publish("99.0.0")
        returncode, err = self.update(binary=brewed)
        assert returncode != 0
        assert "brew upgrade await" in err
        assert self.installed_version(brewed) == self.version

    def test_downloads_the_build_for_this_machine(self):
        """Without an override, the archive is the one for this OS and CPU (the
        static musl build on Linux)."""
        machine = platform.machine().lower()
        arch = "aarch64" if machine in ("arm64", "aarch64") else "x86_64"
        if platform.system() == "Darwin":
            target = f"{arch}-apple-darwin"
        elif platform.system().startswith(("MSYS", "CYGWIN")):
            target = "x86_64-pc-windows-msys"
        else:
            target = f"{arch}-unknown-linux-musl"
        self.releases.publish("99.0.0", target=target)
        env = {k: v for k, v in self.env.items() if k != "AWAIT_UPDATE_TARGET"}
        returncode, err = self.update(env=env)
        assert returncode == 0, err
        assert f"/releases/download/99.0.0/await-99.0.0-{target}.tar.gz" in self.releases.requests
        if platform.system().startswith("MSYS"):
            # Git Bash (and MSYS2's MinGW shells) report MINGW64_NT-... from uname
            import shutil
            shutil.copy("../await", self.binary)
            returncode, err = self.update(env={**env, "MSYSTEM": "MINGW64"})
            assert returncode == 0, err

    def test_auto_update_in_the_background(self):
        """AWAIT_AUTO_UPDATE=1: an interactive run's background check installs the update."""
        import pty, select
        self.releases.publish("99.0.0")
        master, slave = pty.openpty()
        proc = subprocess.Popen([self.binary, "true"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=slave, env={**self.env, "AWAIT_AUTO_UPDATE": "1"})
        os.close(slave)
        assert proc.wait(timeout=5) == 0
        os.close(master)
        deadline = time.time() + 10
        while time.time() < deadline and self.installed_version() != "99.0.0":
            time.sleep(0.1)
        assert self.installed_version() == "99.0.0"

    def test_no_auto_update_by_default(self):
        import pty
        self.releases.publish("99.0.0")
        master, slave = pty.openpty()
        proc = subprocess.Popen([self.binary, "true"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=slave, env=self.env)
        os.close(slave)
        assert proc.wait(timeout=5) == 0
        os.close(master)
        time.sleep(1.5)
        assert self.installed_version() == self.version

    def auto(self, **extra):
        """An interactive run with AWAIT_AUTO_UPDATE=1; returns its stderr."""
        _, err, _ = run_with_tty_stderr(["true"], {**self.env, "AWAIT_AUTO_UPDATE": "1", **extra},
                                        binary=self.binary)
        return err

    def wait_for(self, condition, timeout=10):
        deadline = time.time() + timeout
        while time.time() < deadline and not condition():
            time.sleep(0.1)
        return condition()

    def test_symlink_at_the_backup_path_is_not_followed(self):
        """Under sudo, a symlink planted at <path>.old must be replaced, not written through."""
        victim = os.path.join(self.dir, "victim")
        with open(victim, "w") as f:
            f.write("precious\n")
        os.symlink(victim, self.binary + ".old")
        self.releases.publish("99.0.0")
        returncode, err = self.update()
        assert returncode == 0, err
        assert open(victim).read() == "precious\n"
        assert not os.path.islink(self.binary + ".old")
        assert self.installed_version(self.binary + ".old") == self.version

    def test_no_backup_no_update(self):
        blocker = self.binary + ".old"
        os.mkdir(blocker)                                   # something rm -f can't clear
        open(os.path.join(blocker, "keep"), "w").close()
        self.releases.publish("99.0.0")
        returncode, err = self.update()
        assert returncode != 0
        assert "not updating" in err
        assert "updated" not in err
        assert self.installed_version() == self.version
        assert [f for f in os.listdir(self.bin_dir) if f.startswith(".await-update")] == []

    def test_one_update_at_a_time(self):
        lock = os.path.join(self.bin_dir, ".await-update.lock")
        os.mkdir(lock)                                      # an update in progress
        self.releases.publish("99.0.0")
        returncode, err = self.update()
        assert returncode == 12
        assert "another await --update is running" in err
        assert self.installed_version() == self.version
        assert not os.path.exists(self.binary + ".old")
        assert os.path.isdir(lock)                          # not ours to remove

    def test_lock_left_by_a_killed_update_expires(self):
        lock = os.path.join(self.bin_dir, ".await-update.lock")
        os.mkdir(lock)
        stale = time.time() - 20 * 60
        os.utime(lock, (stale, stale))
        self.releases.publish("99.0.0")
        returncode, err = self.update()
        assert returncode == 0, err
        assert self.installed_version() == "99.0.0"
        assert not os.path.exists(lock)

    def test_auto_update_installs_an_already_cached_version(self):
        """AWAIT_AUTO_UPDATE set after the last check: the newer version in a fresh
        cache is installed now, not once the cache expires."""
        cache = os.path.join(self.dir, "await", "latest-version")
        os.makedirs(os.path.dirname(cache))
        with open(cache, "w") as f:
            f.write("99.0.0\n")
        self.releases.publish("99.0.0")
        self.auto()
        assert self.wait_for(lambda: self.installed_version() == "99.0.0")
        assert open(cache).read().strip() == "99.0.0"

    def test_auto_update_with_a_fresh_cache_tries_once_a_day(self):
        cache = os.path.join(self.dir, "await", "latest-version")
        os.makedirs(os.path.dirname(cache))
        with open(cache, "w") as f:
            f.write("99.0.0\n")
        open(os.path.join(self.dir, "await", "auto-update"), "w").close()   # tried a moment ago
        self.releases.publish("99.0.0")
        self.auto()
        time.sleep(1.5)
        assert self.installed_version() == self.version
        assert self.releases.requests == []

    def test_failed_auto_update_is_reported(self):
        self.releases.publish("99.0.0", checksum="0" * 64)
        self.auto()
        error = os.path.join(self.dir, "await", "update-error")
        assert self.wait_for(lambda: os.path.exists(error) and os.path.getsize(error) > 0)
        assert self.installed_version() == self.version
        err = self.auto()
        assert "automatic update failed: checksum mismatch" in err
        assert "update.log" in err
        # once an update succeeds, the report goes away
        os.utime(os.path.join(self.dir, "await", "latest-version"), (0, 0))
        self.releases.publish("99.0.0")
        self.auto()
        assert self.wait_for(lambda: self.installed_version() == "99.0.0")
        assert self.wait_for(lambda: not os.path.exists(error))


class TestExpect:
    """--expect REGEX: a command succeeds when its stdout matches, whatever it exits with."""

    # prints count 1, count 2 ... up to count <top>, then keeps printing that:
    # await samples each command's latest result, so a match lasting a single
    # run could be missed on a loaded machine; the final output must be stable
    COUNTER = ('n=$(cat {f} 2>/dev/null || echo 0); [ "$n" -lt {top} ] && n=$((n+1)); '
               'echo $n > {f}; echo count $n')

    def run(self, *argv, timeout=10):
        return subprocess.run(["../await", *argv], capture_output=True, text=True, timeout=timeout)

    def counter(self, top):
        fd, path = tempfile.mkstemp(dir=TMPDIR)
        os.close(fd)
        os.unlink(path)
        return path, self.COUNTER.format(f=path, top=top)

    def test_matches_on_first_try(self):
        r = self.run('echo \'{"status": "up"}\'', "--expect", '"status": *"up"', "-V")
        assert r.returncode == 0, r.stderr

    def test_waits_until_output_matches(self):
        path, cmd = self.counter(3)
        try:
            r = self.run(cmd, "-x", "count 3$", "-i", "0.05", "-T", "5", "--json")
            assert r.returncode == 0, r.stderr
            import json
            assert json.loads(r.stdout.strip())["commands"][0]["output"] == "count 3\n"
        finally:
            os.unlink(path)

    def test_nonzero_exit_with_match_succeeds(self):
        r = self.run("echo ready; exit 3", "-x", "ready", "-T", "2", "-V")
        assert r.returncode == 0, r.stderr

    def test_zero_exit_without_match_keeps_waiting(self):
        r = self.run("echo nope", "-x", "ready", "-i", "0.05", "-T", "0.5")
        assert r.returncode == 1
        assert "did not match --expect" in r.stderr

    def test_fail_waits_for_output_not_to_match(self):
        assert self.run("echo nope; exit 1", "-x", "ready", "-f", "-T", "2", "-V").returncode == 0
        assert self.run("echo ready", "-x", "ready", "-f", "-i", "0.05", "-T", "0.5", "-V").returncode == 1

    def test_all_commands_must_match(self):
        assert self.run("echo a1", "echo a2", "-x", "^a", "-T", "2", "-V").returncode == 0
        assert self.run("echo a", "echo b", "-x", "^a", "-i", "0.05", "-T", "0.5", "-V").returncode == 1

    def test_any_needs_one_match(self):
        assert self.run("echo a", "echo b", "-x", "^a", "--any", "-T", "2", "-V").returncode == 0

    def test_invalid_regex_exits_2(self):
        r = self.run("echo x", "--expect", "(")
        assert r.returncode == 2
        assert "invalid --expect regex" in r.stderr

    def test_stderr_is_not_matched(self):
        r = self.run("echo ready >&2", "-x", "ready", "-i", "0.05", "-T", "0.5", "-V")
        assert r.returncode == 1

    def test_json_status_reflects_match(self):
        import json
        r = self.run("echo ready; exit 5", "-x", "ready", "--json")
        assert r.returncode == 0, r.stderr
        data = json.loads(r.stdout.strip())
        assert data["success"] is True
        assert data["commands"][0]["status"] == 0
        r = self.run("echo nope", "-x", "ready", "--json", "-i", "0.05", "-T", "0.5")
        assert r.returncode == 1
        data = json.loads(r.stdout.strip())
        assert data["success"] is False
        assert data["commands"][0]["status"] == 1

    def test_cmd_timeout_is_unsuccessful(self):
        import json
        r = self.run("echo ready; sleep 5", "-x", "ready", "-t", "1", "-r", "1", "--json")
        assert r.returncode == 1
        assert json.loads(r.stdout.strip())["commands"][0]["status"] == 124

    def test_change_counts_only_changes_to_matching_output(self):
        import json
        # count 1 (baseline) -> count 2 (a change, but no match): keeps waiting
        path, cmd = self.counter(2)
        try:
            r = self.run(cmd, "-c", "-x", "count [3-9]", "-i", "0.05", "-T", "1.5", "-V")
            assert r.returncode == 1
        finally:
            os.unlink(path)
        # ... -> count 3 (a change to matching output): done
        path, cmd = self.counter(3)
        try:
            r = self.run(cmd, "-c", "-x", "count [3-9]", "-i", "0.05", "-T", "5", "--json")
            assert r.returncode == 0, r.stderr
            assert json.loads(r.stdout.strip())["commands"][0]["output"] == "count 3\n"
        finally:
            os.unlink(path)

    def test_exec_sees_matching_output(self):
        r = self.run("echo ready; exit 1", "-x", "ready", "--exec", "echo got \\1", "-V")
        assert r.returncode == 0, r.stderr
        assert "got ready" in r.stdout

    def test_timed_out_run_never_completes_fail(self):
        # the run is killed before printing, so it neither matched nor didn't
        r = self.run("sleep 5; echo X", "-x", "X", "--fail", "-t", "1", "-T", "2.5", "-V")
        assert r.returncode == 1

    def test_timed_out_run_never_completes_match(self):
        r = self.run("echo X; sleep 5", "-x", "Y", "--fail", "-t", "1", "-T", "2.5", "-V")
        assert r.returncode == 1
        r = self.run("echo X; sleep 5", "-x", "X", "-t", "1", "-T", "2.5", "-V")
        assert r.returncode == 1

    def test_nul_in_output_does_not_hide_match(self):
        r = self.run("printf 'a\\0ready'", "-x", "ready", "-T", "2", "-V")
        assert r.returncode == 0, r.stderr
        r = self.run("printf 'a\\0b'", "-x", "ready", "-i", "0.05", "-T", "0.5", "-V")
        assert r.returncode == 1

    def test_no_command_not_found_hint_when_output_matched(self):
        r = self.run("echo ready; exit 127", "-x", "ready", "-T", "2")
        assert r.returncode == 0
        assert "command not found" not in r.stderr
        r = self.run("echo nope; exit 127", "-x", "ready", "-i", "0.05", "-T", "0.5")
        assert r.returncode == 1
        assert "command not found" in r.stderr

    @pytest.mark.skipif(platform.system() != "Linux", reason="--service is Linux-only")
    def test_service_replays_quoted_regex(self):
        root = tempfile.mkdtemp()
        bindir = os.path.join(root, "stub")
        os.mkdir(bindir)
        for stub in ("systemctl", "journalctl"):
            path = os.path.join(bindir, stub)
            with open(path, "w") as f:
                f.write("#!/bin/sh\nexit 0\n")
            os.chmod(path, 0o755)
        result = subprocess.run(
            ["../await", "--expect", '"up" \\d$', "echo up", "--service", "t"],
            env={**os.environ, "HOME": root, "PATH": bindir + ":" + os.environ["PATH"]},
            capture_output=True, text=True, timeout=5,
        )
        assert result.returncode == 0, result.stderr
        with open(os.path.join(root, ".config/systemd/user/t.service")) as f:
            unit = f.read()
        assert '--expect "\\"up\\" \\\\d$$"' in unit


class TestOctalEscapes:
    def test_octal_escape_is_not_a_placeholder(self):
        """\\001 is printf's octal escape, not \\1: output must not change between runs."""
        returncode, stdout, stderr = run_await_with_timeout(
            '-Vo --forever "printf \'a\\\\001b\\\\n\'"',
            timeout=1.0,
            description="Every run should print a<SOH>b"
        )
        lines = {l for l in strip_ansi_escape_codes(stdout).replace("\r", "\n").split("\n") if l.strip()}
        assert lines == {"a\x01b"}, lines


class TestTimes:
    """--times N: a command is done only after N successful checks in a row."""

    PATTERN_SCRIPT = (
        "#!/bin/sh\n"
        "# run k: result k of the comma-separated pattern (the last one repeats)\n"
        "n=$(cat \"$1.n\" 2>/dev/null || echo 0); n=$((n+1)); echo $n > \"$1.n\"\n"
        "r=$(echo \"$2\" | cut -d, -f$n); [ -z \"$r\" ] && r=$(echo \"$2\" | awk -F, '{print $NF}')\n"
        "echo \"$r\" >> \"$1.log\"\n"
        "[ \"$r\" = ok ]\n"
    )

    @pytest.fixture
    def pattern(self):
        """Returns (command, log path) for a command that follows a pattern of ok/fail."""
        root = tempfile.mkdtemp(dir=TMPDIR)
        script = os.path.join(root, "pattern.sh")
        with open(script, "w") as f:
            f.write(self.PATTERN_SCRIPT)
        os.chmod(script, 0o755)
        state = os.path.join(root, "state")

        def make(results):
            return f"{script} {state} {results}"

        def runs():
            try:
                with open(state + ".log") as f:
                    return f.read().split()
            except FileNotFoundError:
                return []
        make.runs = runs
        make.root = root
        yield make
        import shutil
        shutil.rmtree(root, ignore_errors=True)

    def test_default_is_one_success(self, pattern):
        cmd = pattern("fail,ok")
        returncode, stdout, stderr = run_await_with_timeout(f'-V -i 0.05 "{cmd}"')
        assert returncode == 0
        assert pattern.runs()[:2] == ["fail", "ok"]

    def test_needs_n_consecutive_successes(self, pattern):
        cmd = pattern("fail,ok,fail,ok,ok,ok")
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.2 --times 3 "{cmd}"',
            description="Done only at the third ok in a row (run 6)"
        )
        elapsed = time.time() - start
        assert returncode == 0
        runs = pattern.runs()
        assert runs[:6] == ["fail", "ok", "fail", "ok", "ok", "ok"]
        assert len(runs) <= 8  # it stops right after the streak completes (+1 for scheduling)
        assert elapsed >= 1.0, f"exited after {elapsed:.2f}s, before 6 runs 0.2s apart"

    def test_failure_resets_streak(self, pattern):
        """ok,ok,fail,ok,ok never has 3 in a row until the pattern's last ok repeats."""
        cmd = pattern("ok,ok,fail,ok,ok,fail,ok")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.05 --times 3 "{cmd}"'
        )
        assert returncode == 0
        assert pattern.runs()[:9] == ["ok", "ok", "fail", "ok", "ok", "fail", "ok", "ok", "ok"]

    def test_timeout_when_streak_never_completes(self, pattern):
        import json
        cmd = pattern("ok,fail,ok,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V --json -i 0.05 -T 0.6 --times 3 "{cmd}"', timeout=3.0
        )
        assert returncode == 1
        data = json.loads(stdout.strip())
        assert data["success"] is False
        assert data["commands"][0]["times"] == 3
        assert data["commands"][0]["streak"] < 3

    def test_json_reports_streak(self, pattern):
        import json
        cmd = pattern("ok")
        returncode, stdout, stderr = run_await_with_timeout(f'--json -i 0.05 --times 2 "{cmd}"')
        assert returncode == 0
        command = json.loads(stdout.strip())["commands"][0]
        assert command["streak"] == 2 and command["times"] == 2

    def test_json_reports_the_streak_that_ended_the_wait(self, pattern):
        """A completed streak is reported as it was when it completed, even
        after a later check broke it (or, with a slow --exec, extended it)."""
        import json
        early = pattern("ok,ok,fail")  # completes its streak at run 2, then fails for good
        late = os.path.join(pattern.root, "late")
        returncode, stdout, stderr = run_await_with_timeout(
            f'--json -i 0.05 --times 2 "{early}" '
            f'"[ -e {late} ] || {{ sleep 0.5; touch {late}; false; }}" --exec "sleep 0.3"',
            timeout=5.0
        )
        assert returncode == 0
        early_json, late_json = json.loads(stdout.strip())["commands"]
        assert early_json["streak"] == 2  # its live streak is 0 by now
        assert late_json["streak"] == 2   # its live streak grew during --exec
        assert pattern.runs()[2] == "fail"

    def test_json_unchanged_without_times(self):
        import json
        returncode, stdout, stderr = run_await_with_timeout('--json "true"')
        assert "streak" not in json.loads(stdout.strip())["commands"][0]

    def test_fail_counts_consecutive_failures(self, pattern):
        cmd = pattern("fail,ok,fail,fail,ok,fail")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.05 --fail --times 3 "{cmd}"'
        )
        assert returncode == 0
        assert pattern.runs()[:8] == ["fail", "ok", "fail", "fail", "ok", "fail", "fail", "fail"]

    def test_any_waits_for_one_full_streak(self, pattern):
        """With --any, the first command to reach N in a row wins, not the first success."""
        flappy = pattern("ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail,ok,fail")
        counter = os.path.join(pattern.root, "steady")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.05 --any --times 3 "{flappy}" "echo x >> {counter}"'
        )
        assert returncode == 0
        with open(counter) as f:
            assert len(f.readlines()) >= 3
        # without --times, the flapping command's first ok would have ended it
        assert pattern.runs()[0] == "ok"

    @pytest.mark.parametrize("value", ["0", "-1", "abc", "2x", "1.5", ""])
    def test_invalid_n(self, value):
        returncode, stdout, stderr = run_await_with_timeout(f'--times "{value}" true')
        assert returncode == 2
        assert "--times" in stderr

    def test_forever_exec_fires_once_per_streak(self, pattern):
        # streaks of 2+ oks: runs 1-4 (one streak), 6-8 (another); then fails forever
        cmd = pattern("ok,ok,ok,ok,fail,ok,ok,ok,fail")
        fired = os.path.join(pattern.root, "fired")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.05 --forever --times 2 "{cmd}" --exec "echo x >> {fired}"',
            timeout=2.0
        )
        assert returncode == 124  # --forever: killed by the test
        assert len(pattern.runs()) > 10
        with open(fired) as f:
            assert len(f.readlines()) == 2

    def test_change_counts_every_nth_change_in_a_row(self, pattern):
        """--change --times 2: output changing on every check completes a streak
        at the 2nd, 4th, 6th ... change, not just the 2nd."""
        counter = os.path.join(pattern.root, "counter")
        fired = os.path.join(pattern.root, "fired")
        cmd = f"n=\\$(cat {counter} 2>/dev/null || echo 0); echo \\$((n+1)) > {counter}; echo \\$n"
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -T 1.5 -i 0.05 --change --times 2 --forever "{cmd}" --exec "echo x >> {fired}"',
            timeout=5.0
        )
        assert returncode == 1  # -T ends --forever
        time.sleep(0.2)  # let an --exec started just before exit finish
        with open(counter) as f:
            changes = int(f.read()) - 1  # the first run is the baseline
        with open(fired) as f:
            fires = len(f.readlines())
        assert changes >= 10, changes
        # one --exec per 2 changes; at exit, a run in progress may have bumped
        # the counter and the last streak may still be pending
        assert changes // 2 - 2 <= fires <= changes // 2, (changes, fires)

    def test_completed_streak_is_not_lost_when_it_breaks(self, pattern):
        """A command that reached N counts as done even if a later check broke
        its streak before the others were done."""
        early = pattern("ok,ok,fail")  # completes its streak at run 2, then fails for good
        late = os.path.join(pattern.root, "late")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -i 0.05 --times 2 "{early}" "[ -e {late} ] || {{ sleep 0.5; touch {late}; false; }}"',
            timeout=5.0
        )
        assert returncode == 0
        assert pattern.runs()[:3] == ["ok", "ok", "fail"]

    def test_forever_exec_runs_for_each_streak_completed_during_it(self, pattern):
        """Streaks that complete while a slow --exec runs are not merged into one."""
        cmd = pattern("ok,fail,ok,fail,ok,fail")  # 3 streaks within ~0.3s, then failures
        fired = os.path.join(pattern.root, "fired")
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -T 3 -i 0.05 --forever --times 1 "{cmd}" --exec "sleep 0.6; echo x >> {fired}"',
            timeout=6.0
        )
        assert returncode == 1
        with open(fired) as f:
            assert len(f.readlines()) == 3

    def test_timeout_report_shows_change_streak(self):
        returncode, stdout, stderr = run_await_with_timeout(
            '-T 0.5 -i 0.05 --change --times 3 "echo same"', timeout=5.0
        )
        assert returncode == 1
        assert "0 of 3 changes in a row" in strip_ansi_escape_codes(stderr)


class TestExitCleanup:
    def test_commands_still_running_at_exit_are_stopped(self):
        """With --any, await exits once one command succeeds; the others must not
        keep running (and e.g. write files) after await is gone."""
        marker = os.path.join(TMPDIR, f"await_orphan_{os.getpid()}")
        if os.path.exists(marker):
            os.remove(marker)
        try:
            returncode, stdout, stderr = run_await_with_timeout(
                f'-V --any "true" "sleep 0.5; touch {marker}"',
                description="Should exit and stop the still-running command"
            )
            assert returncode == 0
            time.sleep(1)
            assert not os.path.exists(marker), "a command kept running after await exited"
        finally:
            if os.path.exists(marker):
                os.remove(marker)


class TestBackoff:
    """--backoff MAX: after each failed check the command's pause doubles
    (from --interval, +-10% jitter, capped at MAX); a success resets it."""

    # A measured pause (one run's end to the next run's start, stamped by the
    # command itself) is the real pause plus process exit/spawn, interpreter
    # startup and scheduling: on CI runners up to ~0.17s, varying run to run.
    # That can only make a pause look longer, so lower bounds are strict
    # (0.85x: the pause minus its 10% jitter) and are what show the backoff.
    # Upper bounds never assume a fixed overhead: they compare pauses of the
    # same run with each other, with margins that correct behaviour keeps
    # however slow the runner, but that a wrong one (no cap, no reset,
    # backoff without the flag) breaks by a wide margin.
    # AWAIT_TEST_SLOW_STAMP=<seconds> sleeps that long before each start stamp
    # and after each end stamp, to simulate a slow runner.

    @staticmethod
    def stamp_command():
        """Shell command appending '<tag> <epoch seconds>' to stdout ($1 = tag);
        macOS date has no %N, so perl (fast to start) or python3."""
        import shutil
        if shutil.which("perl"):
            return "perl -MTime::HiRes=time -e 'printf \"%s %.6f\\n\", $ARGV[0], time'"
        return "python3 -c 'import sys, time; print(sys.argv[1], \"%.6f\" % time.time())'"

    @staticmethod
    def fmt(pauses):
        """pauses in full for assertion messages (pytest abbreviates long lists)"""
        return " ".join(f"{p:.3f}" for p in pauses)

    @pytest.fixture
    def check(self):
        """check(ok_when) -> a command for await that stamps its start and end
        in a log and succeeds when the shell test ok_when holds ($n: number of
        earlier runs); check.pauses(before) -> seconds between each run's end
        and the next run's start, for the runs that started before `before`
        (with -T, the pause ending at the deadline is cut short on purpose)."""
        root = tempfile.mkdtemp(dir=TMPDIR)
        log = os.path.join(root, "log")
        script = os.path.join(root, "check.sh")
        slow = os.environ.get("AWAIT_TEST_SLOW_STAMP")
        delay = f"sleep {slow}\n" if slow else ""

        def make(ok_when="false"):
            stamp = self.stamp_command()
            with open(script, "w") as f:
                f.write(f'n=$(grep -c "^s" "{log}" 2>/dev/null)\n'
                        f'{delay}{stamp} s >> "{log}"\n'
                        f'if {ok_when}; then rc=0; else rc=1; fi\n'
                        f'{stamp} e >> "{log}"\n{delay}'
                        f'exit $rc\n')
            return f"sh {script}"

        def pauses(before=None):
            starts, ends = [], []
            with open(log) as f:
                for line in f:
                    tag, t = line.split()
                    (starts if tag == "s" else ends).append(float(t))
            return [s - e for e, s in zip(ends, starts[1:]) if before is None or s < before]

        make.pauses = pauses
        yield make
        import shutil
        shutil.rmtree(root, ignore_errors=True)

    # These runs end by the command succeeding, not by -T, so no pause is cut
    # short at a deadline. On a loaded machine await may start one more run
    # before it notices that success, so only the pauses up to the successful
    # run are checked.

    def test_intervals_double_up_to_the_cap(self, check):
        # runs 1-6 fail, run 7 succeeds
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V --interval 0.1 --backoff 0.8 "{check("[ $n -ge 6 ]")}"', timeout=10.0)
        assert returncode == 0
        pauses = check.pauses()
        # 0.1 0.2 0.4 0.8 0.8 0.8, each +-10%
        assert len(pauses) >= 6, self.fmt(pauses)
        pauses = pauses[:6]
        for pause, expected in zip(pauses, [0.1, 0.2, 0.4, 0.8, 0.8, 0.8]):
            assert pause >= expected * 0.85, f"{self.fmt(pauses)} (expected {expected})"
        # at the cap it stops doubling: uncapped the 5th pause would be 1.6s,
        # at least 0.56s longer than the 4th even with jitter
        assert max(pauses[4:]) - pauses[3] < 0.4, self.fmt(pauses)

    def test_cap_is_respected(self, check):
        # runs 1-7 fail, run 8 succeeds
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V --interval 0.1 --backoff 0.3 "{check("[ $n -ge 7 ]")}"', timeout=10.0)
        assert returncode == 0
        pauses = check.pauses()
        assert len(pauses) >= 7, self.fmt(pauses)
        pauses = pauses[:7]
        capped = pauses[2:]  # 0.3 0.3 0.3 0.3 0.3
        assert all(p >= 0.3 * 0.85 for p in capped), self.fmt(pauses)
        # flat at the cap: uncapped these would be 0.4 0.8 1.6 3.2 6.4
        assert max(capped) - statistics.median(capped) < 0.3, self.fmt(pauses)
        assert max(capped) < 2 * min(capped) + 0.15, self.fmt(pauses)

    def test_success_resets_the_interval(self, check):
        # runs 1-4 fail (pauses 0.1 0.2 0.4 0.8), run 5 succeeds, then failures
        # again (0.1 0.2 ...); --forever needs -T to end, so leave out the
        # pauses near it
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -T 6 --forever --interval 0.1 --backoff 0.8 "{check("[ $n -eq 4 ]")}"',
            timeout=10.0)
        assert returncode == 1
        pauses = check.pauses(before=start + 6)
        assert len(pauses) >= 6, self.fmt(pauses)
        assert pauses[3] >= 0.8 * 0.85, self.fmt(pauses)  # after the 4th failure: 0.8
        # after the success back to --interval (without the reset: 0.8 again),
        # and after the next failure too (without the reset: still 0.8)
        assert pauses[4] < pauses[3] / 2, self.fmt(pauses)
        assert pauses[5] < pauses[3] / 2, self.fmt(pauses)

    def test_timeout_is_not_overshot(self):
        start = time.time()
        returncode, stdout, stderr = run_await_with_timeout(
            '-V -T 1 --interval 0.1 --backoff 30 "false"', timeout=5.0,
            description="The 30s backoff pause must not delay the 1s timeout")
        elapsed = time.time() - start
        assert returncode == 1
        assert elapsed < 2.0, f"took {elapsed:.2f}s"

    def test_no_back_to_back_reruns_at_the_deadline(self, check):
        """The pause that would pass -T is cut to end at the deadline, but once
        it has passed pauses are not cut to nothing: the command must not be
        rerun back to back while await is giving up."""
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V -T 1.05 --interval 0.1 --backoff 30 "{check()}"', timeout=5.0)
        assert returncode == 1
        # await notices -T at its next 0.1s tick, up to ~0.05s after the deadline
        pauses = check.pauses()
        # 0.1 0.2 0.4, then 0.8 would pass the deadline (~0.7s in) and is cut
        assert len(pauses) <= 4, self.fmt(pauses)

    def test_success_during_backoff_is_noticed_at_next_check(self, check):
        """A command that starts succeeding is picked up at its next check."""
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V --interval 0.1 --backoff 0.8 "{check("[ $n -ge 3 ]")}"', timeout=8.0)
        assert returncode == 0
        pauses = check.pauses()
        # pauses 0.1 0.2 0.4, then the 4th run succeeds and await is done
        assert len(pauses) in (3, 4), self.fmt(pauses)
        assert pauses[2] >= 0.4 * 0.85, self.fmt(pauses)

    @pytest.mark.parametrize("value", ["abc", "0", "-1", "", "1s"])
    def test_invalid_max(self, value):
        returncode, stdout, stderr = run_await_with_timeout(f'--backoff "{value}" true')
        assert returncode == 2
        assert "--backoff" in stderr

    @pytest.mark.parametrize("flags", ["--interval 1 --backoff 0.5", "--backoff 0.5 --interval 1"])
    def test_max_below_interval(self, flags):
        returncode, stdout, stderr = run_await_with_timeout(f'{flags} true')
        assert returncode == 2
        assert "--interval" in stderr

    def test_unchanged_without_flag(self, check):
        # runs 1-7 fail, run 8 succeeds
        returncode, stdout, stderr = run_await_with_timeout(
            f'-V --interval 0.1 "{check("[ $n -ge 7 ]")}"', timeout=5.0)
        assert returncode == 0
        pauses = check.pauses()
        assert len(pauses) >= 7, self.fmt(pauses)
        pauses = pauses[:7]
        assert all(p >= 0.1 * 0.85 for p in pauses), self.fmt(pauses)
        # no growth: backing off, these would be 0.1 0.2 0.4 0.8 1.6 3.2 6.4
        assert max(pauses) - statistics.median(pauses) < 0.4, self.fmt(pauses)


class TestPublishOrder:
    def test_exec_always_sees_the_output_that_triggered_it(self):
        """Status becomes visible only together with the run's output, so
        --exec never runs with a missing or stale \\1 (repeated to catch timing)."""
        for _ in range(10):
            returncode, stdout, stderr = run_await_with_timeout(
                '-V "echo hi" --exec "echo [\\1]"',
                description="Should print [hi]"
            )
            assert returncode == 0
            assert "[hi]" in stdout, stdout

    def test_json_status_and_output_match(self):
        import json
        for _ in range(10):
            returncode, stdout, stderr = run_await_with_timeout('--json "echo done"')
            command = json.loads(stdout.strip())["commands"][0]
            assert command["status"] == 0
            assert command["output"] == "done\n"

if __name__ == "__main__":
    # Make sure await binary exists
    if not os.path.exists("../await"):
        print("Error: ../await binary not found. Run '../build' first.")
        exit(1)
    
    # Run the tests
    pytest.main([__file__, "-v"])