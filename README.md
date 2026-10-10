# BrightWash Laundry Management System

BrightWash is a sample laundry-shop management system for staff and customers. Customers can register, log in, and manage only their own laundry orders. Admins can view and manage all customers' orders. It uses a C++ REST API, SQLite persistence, and an HTML/CSS/JavaScript frontend.

## Problem being solved
Paper-based order tracking can lead to misplaced order details, unclear status updates, and difficulty finding customer records. BrightWash centralizes customer accounts and laundry orders in one small web application.

## Features
- Public responsive landing page with services, pricing examples, and a call to action.
- Register, login, logout, and protected dashboard.
- Passwords stored as PBKDF2-HMAC-SHA256 hashes with unique salts (never plain text).
- Roles: `admin` and `customer`. Public registration always creates a customer; admin creation is controlled by a server-side environment variable.
- Laundry order CRUD: create, read, update, delete; customers only access their own orders, admins can manage all orders.
- Server-side input validation and confirmation before deleting an order.
- SQLite database persists between restarts.

## Tech stack
- C++17, Crow (HTTP framework), SQLite3, OpenSSL
- HTML5, CSS3, vanilla JavaScript
- CMake

## AI disclosure
AI tool used: **ChatGPT (OpenAI)** — used to help plan the project structure, draft starter code, and write setup/documentation guidance. The student should review, test, and be able to explain every part of the code before submission.

## Requirements
- A C++17 compiler
- CMake 3.20+
- Git
- SQLite3 development library and OpenSSL development library
- Internet connection for CMake to fetch Crow and Asio on first configure/build

### Windows (VS Code)
1. Install Visual Studio Code and the **C/C++** and **CMake Tools** extensions.
2. Install Visual Studio Build Tools (Desktop development with C++) or MinGW-w64, CMake, Git, SQLite3, and OpenSSL. Ensure the compiler, CMake, Git, and OpenSSL are on `PATH`.
3. Open this folder in VS Code.
4. In VS Code, choose **Terminal → New Terminal**.
5. Configure and build:
   ```bash
   cmake -S . -B build
   cmake --build build --config Release
   ```
6. Run the server:
   - PowerShell: `$env:PORT="8080"; .\build\Release\brightwash.exe`
   - If your generator places the executable elsewhere, run the `brightwash` executable from the `build` directory.
   - On MinGW/Linux/macOS: `PORT=8080 ./build/brightwash`
7. Open `http://localhost:8080`.

> Crow/Asio are fetched during the CMake configure step. If CMake cannot locate OpenSSL or SQLite3, install their development packages and set `OPENSSL_ROOT_DIR` or `CMAKE_PREFIX_PATH` to the installation location.

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install -y build-essential cmake git libsqlite3-dev libssl-dev
cmake -S . -B build
cmake --build build -j
PORT=8080 ./build/brightwash
```
Then open `http://localhost:8080`.

## Demo accounts
On first run, the server seeds these demo accounts if the database is empty:
- Admin: `admin@brightwash.test` / `AdminDemo!2026`
- Customer: `customer@brightwash.test` / `CustomerDemo!2026`

These are **development-only demo credentials**. Change/remove them before any public deployment. The server creates the admin seed account only when `SEED_DEMO_ACCOUNTS=1` (enabled by default for local evaluation). To create a new admin for a real deployment, set `BOOTSTRAP_ADMIN_EMAIL` and `BOOTSTRAP_ADMIN_PASSWORD` before the first start, then remove those environment variables. Never use demo credentials for real customer data.

## API overview
- `POST /api/register` — customer registration
- `POST /api/login` — login; returns a session token
- `POST /api/logout` — invalidates the token
- `GET /api/me` — current account
- `GET /api/orders` — orders visible to current user
- `POST /api/orders` — create order
- `PUT /api/orders/{id}` — update order
- `DELETE /api/orders/{id}` — delete order

## GitHub repository submission (VS Code)
1. Create a **new empty repository** on GitHub (do not add a README if you already have this one).
2. Extract the project ZIP and open the extracted `brightwash-laundry-system` folder in VS Code.
3. In VS Code select **Terminal → New Terminal**, then run:
   ```bash
   git init
   git add .
   git commit -m "Initial BrightWash laundry management system"
   git branch -M main
   git remote add origin https://github.com/YOUR-USERNAME/YOUR-REPOSITORY.git
   git push -u origin main
   ```
4. Replace the remote URL with your own repository URL. Sign in when Git asks.
5. Refresh your GitHub repository page; confirm the source files and README are present.
6. For later changes in VS Code:
   ```bash
   git add .
   git commit -m "Describe the change"
   git push
   ```
   You can also use the **Source Control** icon in VS Code to stage files, enter a commit message, click **Commit**, then **Sync Changes** / **Push**.

## Deploying online
GitHub stores the source code; **GitHub Pages cannot run this C++ backend**. For a live app, deploy the C++ service to a C++-capable host (for example a VPS or a container host) and configure persistent disk storage for `brightwash.db`. Do not use ephemeral storage if you need database persistence. Set a strong `SESSION_SECRET` and disable demo accounts. Use HTTPS and a production-grade deployment setup before handling real customer data.

## Important security notes
This is an educational starter project, not a production-ready commercial system. Before public use, add rate limiting, CSRF protections where applicable, secure cookie/session handling, database backups, logging, and production secrets management. The demo uses bearer tokens held in browser session storage and PBKDF2 password hashes. Never commit API keys, real passwords, database files, or production secrets.
