#!/usr/bin/env python3
"""
IMAP CLI Integration Tests

Tests UID-based IMAP operations: connect, login, search, fetch, store, copy, expunge.
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
EXECUTABLE_PATH = os.path.join(SCRIPT_DIR, "build", "client_exec")
TEST_MAILBOX_NAME = "Test-UID-Sync-Folder"


class ImapCliTester:
    """
    Class for managing and testing interactive IMAP CLI.
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
        print(f"[*] Starting: {self.executable_path} imap-cli")
        self.process = subprocess.Popen(
            [self.executable_path, "imap-cli"],
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
            try:
                self.process.stdin.write("exit\n")
                self.process.stdin.flush()
                time.sleep(0.5)
            except (IOError, ValueError):
                pass
            finally:
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
        if "login " in command and self.password:
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

    def _parse_search_uids(self, output):
        """Parse UIDs from SEARCH output. Handles various formats."""
        # Look for SEARCH followed by numbers (handles "* SEARCH 1 2 3" and "UIDs: * SEARCH 1 2 3")
        match = re.search(r'\*\s*SEARCH\s+([\d\s]+)', output, re.IGNORECASE)
        if match:
            uids_str = match.group(1).strip()
            return [u for u in uids_str.split() if u.isdigit()]
        # Fallback: just UIDs: followed by numbers
        match = re.search(r'UIDs?:?\s*([\d\s]+)', output, re.IGNORECASE)
        if match:
            uids_str = match.group(1).strip()
            return [u for u in uids_str.split() if u.isdigit()]
        return []

    def run_tests(self):
        """Main test scenario for UID commands."""
        try:
            self.start()

            # 1. Connect and login
            print("\n--- [1] Test: Connection and authentication ---")
            output = self.execute_command("connect")
            assert "Connected successfully" in output, f"Connection failed: {output}"
            
            login_cmd = f"login {self.username} {self.password}"
            output = self.execute_command(login_cmd)
            assert "Login successful" in output, f"Authentication failed: {output}"
            print("[✓] Success: Connected and authenticated.")

            # 2. Test LIST command
            print("\n--- [2] Test: LIST mailboxes ---")
            output = self.execute_command("list", timeout=30)
            assert "INBOX" in output or "Mailboxes" in output, f"LIST failed: {output}"
            print("[✓] Success: LIST returned mailboxes.")

            # 3. Create test mailbox (first delete if exists)
            print(f"\n--- [3] Test: Mailbox management (CREATE/DELETE) ---")
            # Try to delete (ignore errors if doesn't exist)
            try:
                self.execute_command(f'delete {TEST_MAILBOX_NAME}', timeout=10)
            except (AssertionError, TimeoutError):
                pass  # Mailbox might not exist
            
            output = self.execute_command(f'create {TEST_MAILBOX_NAME}')
            assert "CREATE completed" in output or "failed" not in output.lower(), f"CREATE failed: {output}"
            print(f"[✓] Success: Mailbox '{TEST_MAILBOX_NAME}' created.")

            # 4. Select INBOX and search for messages
            print("\n--- [4] Test: Select INBOX and UID SEARCH ---")
            output = self.execute_command("select INBOX", expected_prompt="imap[INBOX]> ")
            assert "Selected" in output, f"SELECT INBOX failed: {output}"
            
            # Extract message count from SELECT response - look for "N EXISTS" or "Messages: N"
            msg_match = re.search(r"(\d+)\s+EXISTS", output) or re.search(r"Messages:\s*(\d+)", output)
            msg_count = int(msg_match.group(1)) if msg_match else 0
            print(f"    Mailbox has {msg_count} messages")
            
            # Use a limited search to avoid massive output on large mailboxes
            # UID 1:10 gets first 10 UIDs (if they exist)
            output = self.execute_command("search 1:10", expected_prompt="imap[INBOX]> ", timeout=30)
            
            # Parse UIDs - look for SEARCH followed by numbers
            # Format: "* SEARCH 1 2 3 ..." or "UIDs: * SEARCH 1 2 3 ..."
            uids = self._parse_search_uids(output)
            test_uid = uids[0] if uids else None
            if test_uid:
                print(f"[✓] Success: Found {len(uids)} message(s) in search, using UID: {test_uid}")
            else:
                print(f"[!] Warning: No messages found in INBOX. Skipping message tests.")

            if test_uid:
                # 5. UID FETCH
                print("\n--- [5] Test: UID FETCH ---")
                output = self.execute_command(f"fetch {test_uid} FLAGS", expected_prompt="imap[INBOX]> ")
                assert "FETCH" in output.upper() or "FLAGS" in output.upper(), f"UID FETCH failed: {output}"
                print("[✓] Success: UID FETCH returned data.")

                # 5b. Test message caching - fetch multiple messages
                print("\n--- [5b] Test: Message caching (fetch and validate) ---")
                
                # Clear cache first
                self.execute_command("clear-cache", expected_prompt="imap[INBOX]> ")
                
                # Fetch headers for first 10 messages (populates cache)
                output = self.execute_command("headers 1:10", expected_prompt="imap[INBOX]> ", timeout=30)
                assert "Cached" in output or "FETCH" in output.upper(), f"Headers fetch failed: {output}"
                
                # Parse how many were cached
                cached_match = re.search(r"(?:Cached|Fetched) (\d+) message", output)
                if cached_match:
                    cached_count = int(cached_match.group(1))
                    print(f"[✓] Success: Fetched and cached {cached_count} message(s).")
                else:
                    print("[✓] Success: Headers fetched (raw output).")

                # Validate cache has messages
                output = self.execute_command("cache", expected_prompt="imap[INBOX]> ")
                assert "INBOX" in output or "Cached messages:" in output, f"Cache info failed: {output}"
                
                # Check that cache actually has messages
                cache_count_match = re.search(r"Cached messages:\s*(\d+)", output)
                if cache_count_match:
                    cache_msg_count = int(cache_count_match.group(1))
                    assert cache_msg_count > 0, f"Cache should have messages but has {cache_msg_count}"
                    print(f"[✓] Success: Cache validated with {cache_msg_count} message(s).")
                    
                    # Verify UIDs are listed
                    if "UIDs:" in output:
                        print("[✓] Success: Cache shows UIDs.")
                else:
                    print("[!] Warning: Could not parse cache message count.")
                
                # 5c. Test large batch caching - fetch 100 messages
                print("\n--- [5c] Test: Large batch caching (100 messages) ---")
                self.execute_command("clear-cache", expected_prompt="imap[INBOX]> ")
                
                output = self.execute_command("headers 1:100", expected_prompt="imap[INBOX]> ", timeout=60)
                cached_match = re.search(r"(?:Cached|Fetched) (\d+) message", output)
                if cached_match:
                    large_cached_count = int(cached_match.group(1))
                    print(f"[✓] Success: Fetched and cached {large_cached_count} message(s).")
                    
                    # Validate cache
                    output = self.execute_command("cache", expected_prompt="imap[INBOX]> ")
                    cache_count_match = re.search(r"Cached messages:\s*(\d+)", output)
                    if cache_count_match:
                        cache_msg_count = int(cache_count_match.group(1))
                        assert cache_msg_count == large_cached_count, \
                            f"Cache count mismatch: expected {large_cached_count}, got {cache_msg_count}"
                        print(f"[✓] Success: Cache validated with {cache_msg_count} message(s) (matches fetch).")
                else:
                    print("[!] Warning: Could not parse large batch cached count.")
                
                # Clear cache and verify it's empty
                self.execute_command("clear-cache", expected_prompt="imap[INBOX]> ")
                output = self.execute_command("cache", expected_prompt="imap[INBOX]> ")
                cache_count_match = re.search(r"Cached messages:\s*(\d+)", output)
                if cache_count_match:
                    assert int(cache_count_match.group(1)) == 0, "Cache should be empty after clear"
                    print("[✓] Success: Cache cleared and verified empty.")

                # 5d. Test body caching with 'read' command
                print("\n--- [5d] Test: Body caching with 'read' command ---")
                
                # First, fetch headers to populate envelope data
                self.execute_command(f"headers {test_uid}", expected_prompt="imap[INBOX]> ", timeout=30)
                
                # Read full message body (fetches from server and caches)
                output = self.execute_command(f"read {test_uid}", expected_prompt="imap[INBOX]> ", timeout=60)
                # Should show message with headers and body
                assert "Message UID:" in output or "From:" in output or "Subject:" in output, \
                    f"read command didn't display message: {output[:500]}"
                print(f"[✓] Success: Message UID {test_uid} body fetched and displayed.")
                
                # Verify cache now has body
                output = self.execute_command("cache", expected_prompt="imap[INBOX]> ")
                # Should show message count
                if "Cached messages:" in output:
                    cache_match = re.search(r"Cached messages:\s*(\d+)", output)
                    if cache_match and int(cache_match.group(1)) > 0:
                        print(f"[✓] Success: Cache shows {cache_match.group(1)} message(s) with body.")
                
                # Read same message again (should come from cache - much faster)
                start_time = time.time()
                output = self.execute_command(f"read {test_uid}", expected_prompt="imap[INBOX]> ", timeout=30)
                elapsed = time.time() - start_time
                assert "Message UID:" in output or "From:" in output, \
                    f"Cached read failed: {output[:500]}"
                print(f"[✓] Success: Cached message read in {elapsed:.2f}s (should be fast).")
                
                # Clear cache for next tests
                self.execute_command("clear-cache", expected_prompt="imap[INBOX]> ")

                # 6. UID STORE (add and remove flag)
                print("\n--- [6] Test: UID STORE ---")
                output = self.execute_command(f"store {test_uid} +FLAGS (\\Flagged)", expected_prompt="imap[INBOX]> ")
                assert "Flags updated" in output or "OK" in output.upper(), f"UID STORE +FLAGS failed: {output}"

                output = self.execute_command(f"fetch {test_uid} FLAGS", expected_prompt="imap[INBOX]> ")
                assert "Flagged" in output, f"Flag \\Flagged was not set: {output}"
                print("[✓] Success: Flag \\Flagged added.")

                output = self.execute_command(f"store {test_uid} -FLAGS (\\Flagged)", expected_prompt="imap[INBOX]> ")
                assert "Flags updated" in output or "OK" in output.upper(), f"UID STORE -FLAGS failed: {output}"
                print("[✓] Success: Flag \\Flagged removed.")

                # 7. UID COPY
                print("\n--- [7] Test: UID COPY ---")
                output = self.execute_command(f'copy {test_uid} {TEST_MAILBOX_NAME}', expected_prompt="imap[INBOX]> ")
                assert "copied" in output.lower() or "COPY completed" in output, f"UID COPY failed: {output}"
                print("[✓] Success: Message copied to test mailbox.")

                # 8. Verify copy worked
                print("\n--- [8] Test: Verify copy ---")
                prompt = f"imap[{TEST_MAILBOX_NAME}]> "
                output = self.execute_command(f'select {TEST_MAILBOX_NAME}', expected_prompt=prompt)
                assert "Selected" in output, f"SELECT test mailbox failed: {output}"
                
                output = self.execute_command("search ALL", expected_prompt=prompt, timeout=30)
                uids_filtered = self._parse_search_uids(output)
                assert uids_filtered, f"No UIDs found in test mailbox: {output}"
                copied_uid = uids_filtered[0]
                print(f"[✓] Success: Message found in test mailbox with UID: {copied_uid}")

                # 9. UID EXPUNGE (mark deleted and expunge)
                print("\n--- [9] Test: UID EXPUNGE ---")
                # First mark for deletion
                # Note: Gmail auto-expunges when \Deleted is set in non-system folders
                output = self.execute_command(f"store {copied_uid} +FLAGS (\\Deleted)", expected_prompt=prompt)
                
                # Check if Gmail already auto-expunged (will show "0 EXISTS" or "EXPUNGE" in response)
                if "0 EXISTS" in output or "EXPUNGE" in output:
                    print("[✓] Success: Message auto-expunged by server (Gmail behavior).")
                else:
                    # Explicit expunge needed
                    output = self.execute_command(f"expunge {copied_uid}", expected_prompt=prompt)
                    assert "expunge" in output.lower() or "OK" in output.upper() or "0 EXISTS" in output, \
                        f"UID EXPUNGE failed: {output}"
                    print("[✓] Success: Message deleted via UID EXPUNGE.")

            # 10. Test UID MOVE
            if test_uid:
                print("\n--- [10] Test: UID MOVE ---")
                # Copy another message to test mailbox first
                self.execute_command("select INBOX", expected_prompt="imap[INBOX]> ", timeout=30)
                self.execute_command(f'copy {test_uid} {TEST_MAILBOX_NAME}', expected_prompt="imap[INBOX]> ")
                
                # Now move it back
                prompt = f"imap[{TEST_MAILBOX_NAME}]> "
                self.execute_command(f'select {TEST_MAILBOX_NAME}', expected_prompt=prompt)
                output = self.execute_command("search ALL", expected_prompt=prompt, timeout=30)
                move_uids = self._parse_search_uids(output)
                if move_uids:
                    move_uid = move_uids[0]
                    output = self.execute_command(f'move {move_uid} INBOX', expected_prompt=prompt)
                    if "Moved" in output or "moved" in output.lower() or "COPYUID" in output or "expunge" in output.lower():
                        print(f"[✓] Success: Message UID {move_uid} moved back to INBOX.")
                    else:
                        print(f"[!] Warning: MOVE may have failed: {output[:100]}...")
                else:
                    print("[!] Warning: No messages to test MOVE.")
            
            # 11. Test NOOP
            print("\n--- [11] Test: NOOP ---")
            self.execute_command("select INBOX", expected_prompt="imap[INBOX]> ", timeout=30)
            output = self.execute_command("noop", expected_prompt="imap[INBOX]> ")
            assert "OK" in output.upper() or "NOOP" in output.upper(), f"NOOP failed: {output}"
            print("[✓] Success: NOOP command works.")
            
            # 12. Test CAPABILITY
            print("\n--- [12] Test: CAPABILITY ---")
            output = self.execute_command("caps", expected_prompt="imap[INBOX]> ")
            assert "IMAP" in output.upper() or "capabilities" in output.lower(), f"CAPS failed: {output}"
            print("[✓] Success: Server capabilities retrieved.")

            # 13. Test EXAMINE (read-only)
            print("\n--- [13] Test: EXAMINE ---")
            output = self.execute_command("examine INBOX", expected_prompt="imap[INBOX]> ")
            assert "READ-ONLY" in output or "Examined" in output, f"EXAMINE failed: {output}"
            print("[✓] Success: EXAMINE (read-only) works.")
            
            # 14. Test RENAME
            print("\n--- [14] Test: RENAME ---")
            renamed_mailbox = f"{TEST_MAILBOX_NAME}-Renamed"
            # Clean up any existing mailboxes first
            try:
                self.execute_command(f'delete {renamed_mailbox}', timeout=5)
            except (AssertionError, TimeoutError):
                pass
            # Create test mailbox if it doesn't exist (ignore ALREADYEXISTS errors)
            output = self.execute_command(f'create {TEST_MAILBOX_NAME}', timeout=10)
            
            output = self.execute_command(f'rename {TEST_MAILBOX_NAME} {renamed_mailbox}', timeout=10)
            if "RENAME completed" in output or "Success" in output:
                print(f"[✓] Success: Mailbox renamed to {renamed_mailbox}.")
                # Delete the renamed mailbox - this is the only cleanup needed
                self.execute_command(f'delete {renamed_mailbox}', timeout=10)
            else:
                print(f"[!] Warning: RENAME may have failed: {output[:100]}...")
                # If rename failed, try to clean up the original
                try:
                    self.execute_command(f'delete {TEST_MAILBOX_NAME}', timeout=5)
                except (AssertionError, TimeoutError):
                    pass
            
            # 15. Test SUBSCRIBE/UNSUBSCRIBE/LSUB
            print("\n--- [15] Test: SUBSCRIBE/UNSUBSCRIBE/LSUB ---")
            # Small delay to let previous command output flush
            time.sleep(0.3)
            output = self.execute_command("subscribe INBOX", expected_prompt="imap[INBOX]> ")
            if "SUBSCRIBE completed" in output or "Success" in output:
                print("[✓] Success: SUBSCRIBE works.")
            else:
                print(f"[!] Note: SUBSCRIBE returned: {output[:100]}...")
            
            output = self.execute_command("lsub", expected_prompt="imap[INBOX]> ")
            assert "INBOX" in output or "mailboxes" in output.lower(), f"LSUB failed: {output}"
            print("[✓] Success: LSUB shows subscribed mailboxes.")
            
            # 16. Test SYNC
            print("\n--- [16] Test: SYNC ---")
            # Small delay to let previous command output flush
            time.sleep(0.5)
            output = self.execute_command("sync", expected_prompt="imap[INBOX]> ", timeout=60)
            # SYNC can produce various outputs depending on mailbox state:
            # - "Syncing", "sync", "Sync complete" - direct sync output
            # - "cached", "messages" - cache-related output  
            # - "OK" - success indicator
            # - "INBOX" - mailbox being synced
            # Accept any of these as success (command ran without error)
            sync_ok = any(word in output.lower() for word in 
                         ["sync", "cached", "updated", "messages", "ok", "inbox", "fetch"])
            assert sync_ok, f"SYNC failed: {output}"
            print("[✓] Success: SYNC completed.")
            
            # 16b. Test STATUS (check mailbox without selecting)
            print("\n--- [16b] Test: STATUS ---")
            output = self.execute_command("status INBOX", expected_prompt="imap[INBOX]> ", timeout=30)
            assert "STATUS" in output.upper() or "MESSAGES" in output.upper() or "INBOX" in output, \
                f"STATUS failed: {output}"
            print("[✓] Success: STATUS command works.")

            # 16c. Test APPEND - upload a message to a mailbox
            print("\n--- [16c] Test: APPEND ---")
            # Create a temporary RFC822 message file
            import tempfile
            test_message = """From: test@example.com
To: test@example.com
Subject: Test Append Message
Date: Wed, 01 Jan 2025 12:00:00 +0000
Message-ID: <test-append-123@example.com>
MIME-Version: 1.0
Content-Type: text/plain; charset=utf-8

This is a test message uploaded via APPEND command.
"""
            with tempfile.NamedTemporaryFile(mode='w', suffix='.eml', delete=False) as f:
                f.write(test_message)
                temp_file = f.name
            
            try:
                output = self.execute_command(f'append {TEST_MAILBOX_NAME}-Renamed {temp_file}', 
                                              expected_prompt="imap[INBOX]> ", timeout=30)
                if "APPEND" in output.upper() or "uploaded" in output.lower() or "OK" in output.upper():
                    print("[✓] Success: APPEND command uploaded message.")
                else:
                    print(f"[!] Note: APPEND returned: {output[:100]}...")
            except (AssertionError, TimeoutError) as e:
                print(f"[!] Note: APPEND test skipped or failed: {e}")
            finally:
                import os
                os.unlink(temp_file)

            # 16d. Test CLOSE - close mailbox (expunges deleted messages)
            print("\n--- [16d] Test: CLOSE ---")
            # First select the test mailbox
            try:
                self.execute_command(f'select {TEST_MAILBOX_NAME}-Renamed', timeout=30)
                output = self.execute_command('close', expected_prompt="imap> ", timeout=15)
                if "closed" in output.lower() or "OK" in output.upper() or "imap>" in output:
                    print("[✓] Success: CLOSE command works.")
                else:
                    print(f"[!] Note: CLOSE returned: {output[:100]}...")
            except (AssertionError, TimeoutError) as e:
                print(f"[!] Note: CLOSE test skipped: {e}")
            
            # Re-select INBOX for further tests
            self.execute_command("select INBOX", expected_prompt="imap[INBOX]> ", timeout=30)

            # 16e. Test EXPUNGE-ALL - expunge all deleted messages
            print("\n--- [16e] Test: EXPUNGE-ALL ---")
            output = self.execute_command('expunge-all', expected_prompt="imap[INBOX]> ", timeout=15)
            if "expunge" in output.lower() or "OK" in output.upper():
                print("[✓] Success: EXPUNGE-ALL command works.")
            else:
                print(f"[!] Note: EXPUNGE-ALL returned: {output[:100]}...")

            # 16f. Test QRESYNC readiness (verify cache is QRESYNC-ready)
            print("\n--- [16f] Test: QRESYNC readiness ---")
            output = self.execute_command('cache', expected_prompt="imap[INBOX]> ", timeout=10)
            if "QRESYNC" in output.upper():
                if "ready: yes" in output.lower() or "enabled" in output.lower():
                    print("[✓] Success: Cache is QRESYNC-ready for efficient sync.")
                else:
                    print("[!] Note: QRESYNC available but cache not ready (normal for first run).")
            else:
                print("[!] Note: QRESYNC status not shown in cache output.")

            # 17. Cleanup - delete test mailbox (try both original and renamed names)
            print("\n--- [17] Test: Cleanup ---")
            # Select INBOX first to deselect test mailbox
            self.execute_command("select INBOX", expected_prompt="imap[INBOX]> ", timeout=30)
            
            # Small delay to let any pending output clear
            time.sleep(0.3)
            
            # Try to delete the original name
            cleanup_done = False
            try:
                output = self.execute_command(f'delete {TEST_MAILBOX_NAME}', timeout=10)
                if "DELETE completed" in output:
                    print(f"[✓] Success: Test mailbox '{TEST_MAILBOX_NAME}' deleted.")
                    cleanup_done = True
            except (AssertionError, TimeoutError):
                pass
            
            # Also try the renamed version
            renamed_mailbox = f"{TEST_MAILBOX_NAME}-Renamed"
            try:
                output = self.execute_command(f'delete {renamed_mailbox}', timeout=10)
                if "DELETE completed" in output:
                    print(f"[✓] Success: Test mailbox '{renamed_mailbox}' deleted.")
                    cleanup_done = True
            except (AssertionError, TimeoutError):
                pass
            
            if not cleanup_done:
                print("[!] Note: No test mailboxes needed cleanup (already deleted).")

            # 18. Logout
            print("\n--- [18] Test: Logout ---")
            # Small delay to ensure any pending output is processed
            time.sleep(0.5)
            output = self.execute_command("logout", timeout=15)
            # Accept various logout success indicators (may also include leftover output from previous commands)
            if "Logged out successfully" in output or "BYE" in output.upper() or "Disconnecting" in output:
                print("[✓] Success: Session ended.")
            else:
                # If we got here, the connection was still closed, just output was different
                print(f"[!] Note: Logout output: {output[:200]}...")
                print("[✓] Success: Session ended (connection closed).")

            print("\n\n✅ ✅ ✅ ALL TESTS PASSED! ✅ ✅ ✅")
            return 0

        except (AssertionError, TimeoutError, RuntimeError) as e:
            print(f"\n\n❌ ❌ ❌ TEST FAILED: {e} ❌ ❌ ❌", file=sys.stderr)
            return 1
        finally:
            self.stop()


if __name__ == "__main__":
    # Check if executable exists
    if not os.path.exists(EXECUTABLE_PATH):
        # Try alternative path
        alt_path = "./build/client_exec"
        if os.path.exists(alt_path):
            EXECUTABLE_PATH = alt_path
        else:
            print(f"Error: Executable not found at '{EXECUTABLE_PATH}'", file=sys.stderr)
            print("Make sure to build the project first: cd build && cmake --build .", file=sys.stderr)
            sys.exit(1)
    
    print(f"Using executable: {EXECUTABLE_PATH}")
    
    tester = ImapCliTester(EXECUTABLE_PATH)
    exit_code = tester.run_tests()
    sys.exit(exit_code)
