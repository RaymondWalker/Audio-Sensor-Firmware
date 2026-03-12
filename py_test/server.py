import socket
from sec import host_ip, port

# Bind to all interfaces so ESP32 can reach it
HOST = '0.0.0.0'  # Listen on all network interfaces
PORT = int(port)
is_running = True

print(f"Starting server on {HOST}:{PORT}")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # Allow quick restart
    s.bind((HOST, PORT))
    s.listen()
    print(f"Listening for connections...")
    
    while True:  # Keep accepting new connections
        conn, addr = s.accept()
        with conn:
            print(f"Connected by {addr}")
            
            # Receive data
            data = conn.recv(1024)
            if data:
                print(f"Received: {data.decode('utf-8', errors='ignore')}")
                
                # Send response
                response = b"Authenticated\n"
                conn.sendall(response)
                print(f"Sent response")
                while is_running:
                    print(conn.recv(1024))
                    if KeyboardInterrupt:
                        is_running = False
                    else: 
                        is_running = True
                        
            else:
                print("No data received")