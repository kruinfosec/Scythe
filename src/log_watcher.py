import time
import os
import re
from sqlalchemy.orm import sessionmaker
from sqlalchemy import create_engine
from database_manager import Session as DbSession, Terminal, Log, DATABASE_URL, create_database

INPUT_MARKER = "---SCYTHE-INPUT-MARKER---"

def clean_input_buffer(raw_buffer):
    """
    Processes a raw input buffer to find and clean the command string.
    - Strips shell prompts.
    - Handles backspace characters.
    """
    # Heuristically find the prompt. This looks for common prompt endings
    # and assumes the command is what comes after it.
    prompt_pattern = re.compile(r'(?:[$#%>]\s*|\[\S+@\S+\s+\S+\]$\s*)$')
    match = prompt_pattern.search(raw_buffer)
    
    if match:
        command_text = raw_buffer[match.end():]
    else:
        command_text = raw_buffer

    # Process backspaces and other non-printables
    out = []
    for char in command_text:
        if char in ('\x08', '\x7f'):  # Handle Backspace/DEL
            if out:
                out.pop()
        elif char.isprintable():
            out.append(char)
            
    return "".join(out).strip()

def watch_log_file(log_file_path, db_session, terminal_id):
    """
    Monitors a log file, processes the raw stream to identify command
    inputs and outputs, and logs them to the database.
    """
    print(f"Log watcher started. Waiting for commands...")
    
    try:
        # Ensure log file is fresh
        if os.path.exists(log_file_path):
            os.remove(log_file_path)
        with open(log_file_path, 'w') as f:
            pass

        with open(log_file_path, 'r') as f:
            buffer = ""
            last_input_log_id = None

            while True:
                new_content = f.read()
                if not new_content:
                    time.sleep(0.05)
                    continue
                
                buffer += new_content
                
                while INPUT_MARKER in buffer:
                    parts = buffer.split(INPUT_MARKER, 1)
                    transaction_block = parts[0]
                    buffer = parts[1]

                    # The transaction_block contains the output of the previous command
                    # plus the raw input of the new command.
                    
                    # Separate output from the new raw input. The raw input is
                    # assumed to be the last line of the block.
                    split_pos = transaction_block.rfind('\n')
                    if split_pos != -1:
                        output_data = transaction_block[:split_pos]
                        raw_input_data = transaction_block[split_pos+1:]
                    else:
                        output_data = ""
                        raw_input_data = transaction_block

                    # Log the output of the previous command
                    if output_data and last_input_log_id:
                        output_log = Log(
                            terminal_id=terminal_id,
                            output=output_data,
                            input_log_id=last_input_log_id
                        )
                        db_session.add(output_log)
                        db_session.commit()
                        print(f"Logged Output for command ID {last_input_log_id}")

                    # Clean and log the new command
                    clean_command = clean_input_buffer(raw_input_data)
                    if clean_command:
                        input_log = Log(
                            terminal_id=terminal_id,
                            command=clean_command
                        )
                        db_session.add(input_log)
                        db_session.commit()
                        last_input_log_id = input_log.id
                        print(f"Logged Input: '{clean_command}' (ID: {last_input_log_id})")

    except FileNotFoundError:
        print(f"Log file not found: {log_file_path}. Waiting for it to be created...")
        time.sleep(2)
        watch_log_file(log_file_path, db_session, terminal_id)
    except Exception as e:
        print(f"An error occurred in log watcher: {e}")
        import traceback
        traceback.print_exc()

def main():
    """Main function to set up the database session and start watching the log."""
    create_database()
    
    log_file = "/tmp/scythe_session.log"

    if os.path.exists(log_file):
        os.remove(log_file)

    engine = create_engine(DATABASE_URL)
    SessionMaker = sessionmaker(bind=engine)
    db_session = SessionMaker()

    new_scythe_session = DbSession()
    db_session.add(new_scythe_session)
    db_session.commit()

    main_terminal = Terminal(session_id=new_scythe_session.id, name="Main Terminal")
    db_session.add(main_terminal)
    db_session.commit()

    print(f"New session started (ID: {new_scythe_session.id}), Main Terminal (ID: {main_terminal.id})")

    try:
        watch_log_file(log_file, db_session, main_terminal.id)
    finally:
        db_session.close()

if __name__ == "__main__":
    main()