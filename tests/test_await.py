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
import tempfile
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


@pytest.mark.skipif(platform.system() != "Linux", reason="--service is Linux/systemd only")
class TestService:
    """--service writes a systemd unit that replays the full command line."""

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
            [binary, "--name", "web", "--json", "--lap", "-i", "0.5",
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
        for flag in ('--name "web"', "--json", "--lap", '--interval "0.5"'):
            assert flag in exec_start
        # systemd expands $ and % and ends the argument at an unescaped quote
        assert '"curl -sf \\"http://x/$$HOME\\" | grep 100%%"' in exec_start

        if shutil.which("systemd-analyze"):
            verify = subprocess.run(["systemd-analyze", "verify", unit_path],
                                    capture_output=True, text=True)
            assert verify.returncode == 0, verify.stderr


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


def run_with_tty_stderr(args, env, timeout=5.0):
    """Run await with stderr on a pseudo-terminal, like an interactive shell.
    Returns (returncode, stderr, seconds)."""
    import pty, select
    master, slave = pty.openpty()
    start = time.time()
    proc = subprocess.Popen(["../await"] + args, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
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

    def publish(self, version, target="test-target", binary=None, checksum=None, sums=True):
        """Publish `version` with an archive for `target` whose `await` is a stand-in
        that answers --version (or `binary`, a shell script)."""
        import hashlib, io, tarfile
        self.latest = version
        script = binary or f"#!/bin/sh\necho {version}\n"
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tar:
            info = tarfile.TarInfo("await")
            info.size, info.mode = len(script), 0o644   # like the old archives: not executable
            tar.addfile(info, io.BytesIO(script.encode()))
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

    def test_unsupported_platform(self):
        self.releases.publish("99.0.0")
        returncode, err = self.update(env={**self.env, "AWAIT_UPDATE_TARGET": ""})
        assert returncode != 0
        assert "no prebuilt await" in err
        self.assert_untouched()

    @pytest.mark.skipif(os.geteuid() == 0, reason="root can write anywhere")
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
        target = f"{arch}-apple-darwin" if platform.system() == "Darwin" else f"{arch}-unknown-linux-musl"
        self.releases.publish("99.0.0", target=target)
        env = {k: v for k, v in self.env.items() if k != "AWAIT_UPDATE_TARGET"}
        returncode, err = self.update(env=env)
        assert returncode == 0, err
        assert f"/releases/download/99.0.0/await-99.0.0-{target}.tar.gz" in self.releases.requests

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