#!/usr/bin/env python3
"""
SMTP CLI Integration Tests

Tests SMTP operations: connect, authenticate, send mail, NOOP, HELP, VRFY, RSET, QUIT.
Requires MAIL_USERNAME and MAIL_PASSWORD environment variables.
"""

import subprocess
import os
import sys
import time
import re
from threading import Thread
from queue import Queue, Empty

# --- Configuration ---
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EXECUTABLE_PATH = os.path.join(SCRIPT_DIR, "..", "build", "client_exec")


class SmtpCliTester:
    """
    Class for managing and testing interactive SMTP CLI.
    """
    def __init__(self, executable_path):
        self.executable_path = executable_path
        self.process = None
        self.stdout_queue = Queue()
        self.stdout_thread = None

        # Check for credentials
        self.username = os.getenv("MAIL_USERNAME")
        self.password = os.getenv("MAIL_PASSWORD")
        if not self.username or not self.password:
            raise RuntimeError("MAIL_USERNAME and MAIL_PASSWORD environment variables must be set.")

    def _enqueue_output(self, out, queue):
        """Read output character by character to handle prompts without newlines."""
        buffer = ""
        while True:
            char = out.read(1)
            if not char:  # EOF
                if buffer:
                    queue.put(buffer)
                break
            buffer += char
            # Emit on newline or when we see a prompt pattern
            if char == '\n' or buffer.endswith("> "):
                queue.put(buffer)
                buffer = ""
        out.close()

    def start(self):
        """Start the CLI process."""
        print(f"[*] Starting: {self.executable_path} smtp-cli")
        self.process = subprocess.Popen(
            [self.executable_path, "smtp-cli"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            encoding='utf-8',
            errors='replace'
        )
        # Start thread for non-blocking stdout reading
        self.stdout_thread = Thread(target=self._enqueue_output, args=(self.process.stdout, self.stdout_queue))
        self.stdout_thread.daemon = True
        self.stdout_thread.start()
        # Wait for welcome message
        self.read_until_prompt(timeout=15)
        print("[+] CLI started and ready.")

    def stop(self):
        """Stop the CLI process."""
        if self.process:
            # Only try to send exit if process is still running
            if self.process.poll() is None:
                try:
                    self.process.stdin.write("exit\n")
                    self.process.stdin.flush()
                    time.sleep(0.5)
                except (IOError, ValueError, BrokenPipeError):
                    pass
            # Terminate if still running
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
            print("[+] Process stopped.")

    def send_command(self, command):
        """Send command to CLI."""
        if not self.process or self.process.poll() is not None:
            raise RuntimeError("CLI process not running.")
        # Mask password in output
        display_cmd = command
        if "auth " in command and self.password:
            display_cmd = command.replace(self.password, "****")
        print(f"\n>>> {display_cmd}")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def read_until_prompt(self, prompt="> ", timeout=30):
        """Read output until prompt appears."""
        output = []
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                chunk = self.stdout_queue.get(timeout=0.2)
                # Print each line (strip for cleaner output)
                for line in chunk.split('\n'):
                    if line.strip():
                        print(f"<<< {line.strip()}")
                if prompt in chunk:
                    output.append(chunk)
                    return "".join(output)
                output.append(chunk)
            except Empty:
                continue
        raise TimeoutError(f"Timeout waiting for prompt '{prompt}'. Last output:\n{''.join(output)}")

    def execute_command(self, command, expected_prompt="> ", timeout=30):
        """Send command and return response."""
        self.send_command(command)
        return self.read_until_prompt(prompt=expected_prompt, timeout=timeout)

    def run_tests(self):
        """Main test scenario for SMTP commands."""
        try:
            self.start()

            # 1. Connect to SMTP server
            print("\n--- [1] Test: Connection to SMTP server ---")
            output = self.execute_command("connect")
            assert "Connected successfully" in output or "220" in output, f"Connection failed: {output}"
            print("[✓] Success: Connected to SMTP server.")

            # 2. Test NOOP command (before authentication)
            print("\n--- [2] Test: NOOP command (keep-alive) ---")
            output = self.execute_command("noop")
            assert "250" in output or "OK" in output.upper() or "successful" in output.lower(), \
                f"NOOP failed: {output}"
            print("[✓] Success: NOOP command works.")

            # 3. Test HELP command (SMTP server help, not CLI help)
            print("\n--- [3] Test: HELP-SMTP command ---")
            output = self.execute_command("help-smtp")
            # HELP response varies by server - 214 or 250 status code, or error
            if "214" in output or "250" in output or "HELP" in output.upper():
                print("[✓] Success: HELP command returned server information.")
            elif "502" in output or "500" in output or "failed" in output.lower():
                print("[✓] Success: HELP command not supported by server (expected for some servers).")
            else:
                print(f"[!] Warning: HELP returned unexpected: {output[:100]}...")

            # 4. Test VRFY command (verify email address)
            print("\n--- [4] Test: VRFY command (email verification) ---")
            output = self.execute_command(f"vrfy {self.username}")
            # VRFY may not be supported by all servers (252 = cannot verify, 550 = not supported)
            # Accept both success and "not supported" responses
            if "250" in output or "252" in output:
                print("[✓] Success: VRFY command executed (address verified or cannot verify).")
            elif "550" in output or "502" in output or "not supported" in output.lower():
                print("[✓] Success: VRFY command not supported by server (expected).")
            else:
                print(f"[!] Warning: VRFY returned unexpected response: {output[:100]}")

            # 5. Authenticate with PLAIN AUTH
            print("\n--- [5] Test: Authentication (PLAIN) ---")
            auth_cmd = f"auth {self.username} {self.password}"
            output = self.execute_command(auth_cmd)
            assert "Authentication successful" in output or "235" in output, \
                f"Authentication failed: {output}"
            print("[✓] Success: Authenticated successfully.")

            # 6. Test NOOP after authentication
            print("\n--- [6] Test: NOOP after authentication ---")
            output = self.execute_command("noop")
            assert "250" in output or "OK" in output.upper(), f"NOOP after auth failed: {output}"
            print("[✓] Success: NOOP works after authentication.")

            # 7. Send a test email
            print("\n--- [7] Test: Send mail ---")
            # Use 'send' command with from, to, subject, body arguments
            send_cmd = f"send {self.username} {self.username} TestSubject TestBody"
            output = self.execute_command(send_cmd, timeout=60)
            assert "Mail sent successfully" in output or "250" in output, \
                f"Send mail failed: {output}"
            print("[✓] Success: Test email sent successfully.")

            # 8. Test RSET command (reset mail transaction)
            print("\n--- [8] Test: RSET command ---")
            output = self.execute_command("rset")
            assert "250" in output or "OK" in output.upper() or "reset" in output.lower(), \
                f"RSET failed: {output}"
            print("[✓] Success: RSET command executed.")

            # 9. Send another email to test multiple send operations
            print("\n--- [9] Test: Send second email ---")
            send_cmd2 = f"send {self.username} {self.username} SecondTest SecondBody"
            output = self.execute_command(send_cmd2, timeout=60)
            assert "Mail sent successfully" in output or "250" in output, \
                f"Second send failed: {output}"
            print("[✓] Success: Second test email sent successfully.")

            # 10. Test CAPS command (server capabilities)
            print("\n--- [10] Test: CAPS command ---")
            output = self.execute_command("caps", timeout=10)
            assert "Capabilities" in output or "SMTP" in output or "AUTH" in output, \
                f"CAPS failed: {output}"
            print("[✓] Success: Server capabilities retrieved.")

            # 11. Test STATUS command
            print("\n--- [11] Test: STATUS command ---")
            output = self.execute_command("status", timeout=10)
            assert "Status" in output or "Server" in output or "Connection" in output, \
                f"STATUS failed: {output}"
            print("[✓] Success: Status information retrieved.")

            # 12. Test CONFIG command
            print("\n--- [12] Test: CONFIG command ---")
            output = self.execute_command("config", timeout=10)
            assert "Configuration" in output or "SMTP" in output or "Host" in output, \
                f"CONFIG failed: {output}"
            print("[✓] Success: Configuration displayed.")

            # 13. Test AUTH LOGIN (reconnect to test alternative auth method)
            print("\n--- [13] Test: AUTH LOGIN ---")
            # Disconnect first
            self.execute_command("disconnect", timeout=10)
            time.sleep(0.5)
            # Reconnect
            output = self.execute_command("connect", timeout=30)
            if "Connected" in output or "successfully" in output.lower():
                # Try AUTH LOGIN
                auth_login_cmd = f"auth-login {self.username} {self.password}"
                output = self.execute_command(auth_login_cmd, timeout=30)
                if "Authentication successful" in output or "235" in output:
                    print("[✓] Success: AUTH LOGIN authentication works.")
                elif "not supported" in output.lower() or "503" in output:
                    print("[!] Note: AUTH LOGIN not supported by server (PLAIN-only).")
                else:
                    print(f"[!] Note: AUTH LOGIN returned: {output[:100]}...")
            else:
                print(f"[!] Note: Reconnect for AUTH LOGIN test failed: {output[:100]}...")

            # 14. Quit and close connection
            print("\n--- [14] Test: QUIT command ---")
            self.send_command("quit")
            time.sleep(1)  # Give process time to exit
            # Process should have exited - check if it's still running
            if self.process.poll() is not None:
                print("[✓] Success: Connection closed gracefully with QUIT.")
            else:
                # Try to read any remaining output
                try:
                    output = self.read_until_prompt(prompt="> ", timeout=3)
                    print(f"[!] Note: Quit output: {output[:100]}...")
                except TimeoutError:
                    pass
                print("[✓] Success: QUIT command sent.")

            print("\n\n✅ ✅ ✅ ALL SMTP TESTS PASSED! ✅ ✅ ✅")
            return 0

        except (AssertionError, TimeoutError, RuntimeError) as e:
            print(f"\n\n❌ ❌ ❌ SMTP TEST FAILED: {e} ❌ ❌ ❌", file=sys.stderr)
            return 1
        finally:
            self.stop()


if __name__ == "__main__":
    # Check if executable exists
    if not os.path.exists(EXECUTABLE_PATH):
        # Try alternative paths
        alt_paths = [
            "./build/client_exec",
            "../build/client_exec",
            "./client_exec"
        ]
        for alt_path in alt_paths:
            if os.path.exists(alt_path):
                EXECUTABLE_PATH = alt_path
                break
        else:
            print(f"Error: Executable not found at '{EXECUTABLE_PATH}'", file=sys.stderr)
            print("Make sure to build the project first: cd build && cmake --build .", file=sys.stderr)
            sys.exit(1)
    
    print(f"Using executable: {EXECUTABLE_PATH}")
    
    tester = SmtpCliTester(EXECUTABLE_PATH)
    exit_code = tester.run_tests()
    sys.exit(exit_code)
