# Push this mod to GitHub (private repo)

## 1. Create a **private** repo on GitHub

1. Go to [https://github.com/new](https://github.com/new)
2. **Repository name:** e.g. `StarsandIslandMod` (or any name you like)
3. Set visibility to **Private**
4. **Do not** add a README, .gitignore, or license (this folder already has them)
5. Click **Create repository**

## 2. Add remote and push (replace with your repo URL)

In PowerShell, from this folder (`StarsandIslandMod`):

```powershell
cd "C:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\StarsandIslandMod"

# Add your GitHub repo as remote (replace YOUR_USERNAME and YOUR_REPO with your actual GitHub username and repo name)
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPO.git

# Push main and the develop branch
git push -u origin main
git push -u origin develop
```

If GitHub asks for auth, use a **Personal Access Token** (Settings → Developer settings → Personal access tokens) as the password when prompted.

## 3. Branches you have

- **main** – current source (initial commit)
- **develop** – branch off main for future work

To work on the branch:

```powershell
git checkout develop
# make changes, then:
git add -A
git commit -m "Your message"
git push origin develop
```
