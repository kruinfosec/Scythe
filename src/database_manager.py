import os
from sqlalchemy import create_engine, Column, Integer, String, DateTime, Text, ForeignKey
from sqlalchemy.orm import sessionmaker, relationship
from sqlalchemy.ext.declarative import declarative_base
import datetime

# Get the config directory for Scythe
config_dir = os.path.join(os.path.expanduser("~"), ".config", "Scythe")
if not os.path.exists(config_dir):
    os.makedirs(config_dir)

DATABASE_URL = f"sqlite:///{os.path.join(config_dir, 'scythe.db')}"

Base = declarative_base()

class Session(Base):
    __tablename__ = 'sessions'
    id = Column(Integer, primary_key=True)
    start_time = Column(DateTime, default=datetime.datetime.utcnow)
    name = Column(String, default="Scythe Session")
    terminals = relationship("Terminal", back_populates="session")

class Terminal(Base):
    __tablename__ = 'terminals'
    id = Column(Integer, primary_key=True)
    session_id = Column(Integer, ForeignKey('sessions.id'))
    name = Column(String, default="Terminal")
    session = relationship("Session", back_populates="terminals")
    logs = relationship("Log", back_populates="terminal")

class Log(Base):
    __tablename__ = 'logs'
    id = Column(Integer, primary_key=True)
    terminal_id = Column(Integer, ForeignKey('terminals.id'))
    timestamp = Column(DateTime, default=datetime.datetime.utcnow)
    
    # Restructured columns for clear input/output logging
    command = Column(Text, nullable=True) # Stores the clean command string
    output = Column(Text, nullable=True) # Stores the output of the command
    
    # Self-referential foreign key to link output to a command
    input_log_id = Column(Integer, ForeignKey('logs.id'), nullable=True)
    
    terminal = relationship("Terminal", back_populates="logs")
    
    # Relationship to link a command to its output
    output_logs = relationship("Log", foreign_keys=[input_log_id])

def create_database():
    """Creates the database and tables if they don't exist."""
    engine = create_engine(DATABASE_URL)
    Base.metadata.create_all(engine)
    print(f"Database created and tables initialized at: {DATABASE_URL}")

if __name__ == "__main__":
    print("Initializing Scythe database...")
    create_database()
