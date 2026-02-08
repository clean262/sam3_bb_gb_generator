# Nvidia driver install

# uv install
```bash
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
uv --version  # powershell再起動後表示が出ればOK
```

# file install
```bash
git clone 
uv sync
uv run hf auth login # access tokenを発行(https://huggingface.co/settings/tokens)し入力(絶対公開しないでください) Add token as git credential?にはnでOK
uv run python -c "from huggingface_hub import whoami; print(whoami())" # access_tokenが出るならOK
uv run python -c "import torch; print('cuda:', torch.cuda.is_available())" # cuda: TrueならOK
uv run python app.py
```