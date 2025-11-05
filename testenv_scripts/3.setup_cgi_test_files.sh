#!/bin/bash
# ============================
# Setup CGI test files
# ============================

CGI_DIR=./www/cgi-bin
mkdir -p $CGI_DIR

# --- ok.py ---
cat > $CGI_DIR/ok.py <<'EOF'
#!/usr/bin/env python3
print("Status: 200 OK")
print("Content-Type: text/plain")
print()
print("Hello from CGI OK")
EOF
chmod +x $CGI_DIR/ok.py

# --- no_status.py ---
cat > $CGI_DIR/no_status.py <<'EOF'
#!/usr/bin/env python3
print("Content-Type: text/plain")
print()
print("No status header, default should be 200 OK")
EOF
chmod +x $CGI_DIR/no_status.py

# --- error.py ---
cat > $CGI_DIR/error.py <<'EOF'
#!/usr/bin/env python3
import sys
sys.exit(1)
EOF
chmod +x $CGI_DIR/error.py

echo "✅ CGI test files created in $CGI_DIR"

