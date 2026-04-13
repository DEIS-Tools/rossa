set -e
 
# Path to rossa package (where pyproject.toml lives)
ROSSA_PATH=..

# Setup Python Virtual Environment
echo "Creating virtual Python environment in 'venv'"
python3 -m venv venv
source venv/bin/activate
# Install it in -editable mode such any source changes to rossa is effective immediately.
pip install -e $ROSSA_PATH
pip install plumbum
