set -e
 
# Path to rnetwork package (where pyproject.toml lives)
RNETWORK_PATH=..

# Setup Python Virtual Environment
echo "Creating virtual Python environment in 'venv'"
python3 -m venv venv
source venv/bin/activate
# Install it in -editable mode such any source changes to rnetwork is effective immediately.
pip install -e $RNETWORK_PATH
pip install plumbum


