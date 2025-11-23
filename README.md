# Debug Test Project

This project demonstrates session reuse debugging with client-server architecture, using Harbor as the container registry and Kubernetes (kind) for orchestration.

## Prerequisites

- Docker Desktop (or Docker Engine)
- Kubernetes cluster (kind is used in this project)
- Harbor registry running locally at `harbor.com`
- kubectl configured to access your cluster

## Harbor Registry Setup

This project uses Harbor as the container registry. Harbor is configured to run at `https://harbor.com` with:
- **Default credentials**: `admin` / `Harbor12345`
- **Project**: `library` (public project)

### Harbor Status

To check if Harbor is running:
```bash
curl -k https://harbor.com/api/v2.0/health
# Should return: 200
```

To start Harbor (if not running):
```bash
cd /Users/zaedinzeng/projects/harbor/harbor
docker compose up -d
```

## Project Structure

```
debug_test/
├── client/
│   ├── Dockerfile
│   ├── main.c
│   └── session_reuse_client.yaml    # Kubernetes Pod manifest
├── server/
│   ├── Dockerfile
│   ├── main.c
│   └── session_reuse_server.yaml    # Kubernetes Deployment + Service
├── kind-config.yaml                  # Kind cluster configuration
├── setup-harbor-kind.sh             # Script to configure Harbor access
└── README.md                         # This file
```

## Quick Start

### 1. Setup Kind Cluster

Create a kind cluster with the provided configuration:
```bash
kind create cluster --config kind-config.yaml --name kind
```

### 2. Configure Harbor Access

Run the setup script to make Harbor accessible from the kind cluster:
```bash
./setup-harbor-kind.sh
```

This script:
- Updates `/etc/hosts` on all kind nodes to resolve `harbor.com`
- Creates a Kubernetes secret for Harbor authentication

**Note**: You need to run this script after creating or recreating the kind cluster.

### 3. Build and Push Images

Build and push the client and server images to Harbor:

```bash
# Login to Harbor
docker login harbor.com -u admin -p Harbor12345

# Build and push client image
cd client
docker build -t harbor.com/library/session-reuse-client:1 .
docker push harbor.com/library/session-reuse-client:1

# Build and push server image
cd ../server
docker build -t harbor.com/library/session-reuse-server:1 .
docker push harbor.com/library/session-reuse-server:1
```

### 4. Deploy to Kubernetes

Apply the Kubernetes manifests:
```bash
kubectl apply -f server/session_reuse_server.yaml
kubectl apply -f client/session_reuse_client.yaml
```

Verify pods are running:
```bash
kubectl get pods -o wide
```

## Remote Debugging with VS Code

This project supports remote debugging of both client and server applications using GDB/gdbserver through VS Code.

### Prerequisites for Debugging

1. **VS Code C/C++ Extension**: Install the "C/C++" extension by Microsoft
2. **GDB**: Install GDB on your local machine (macOS: `brew install gdb`)
3. **Port Forwarding**: Set up port forwarding for gdbserver (port 7777 for server, 7778 for client)

### Correct Configuration for Breakpoints

**Key Insight**: When VS Code loads the local copy of the remote binary (`app.remote`), GDB resolves source paths relative to where the binary is located (macOS path), not the remote `/src` path. Therefore, **do not use `sourceFileMap`** for breakpoint resolution.

#### Dockerfile Configuration

The Dockerfile must compile with debug symbols that match what GDB will see:

```dockerfile
# Force GCC to record /src paths in debug symbols (regardless of actual build path)
RUN gcc -O0 -g -Wall -fdebug-prefix-map=$(pwd)=/src -o app main.c -lssl -lcrypto
```

**Note**: The `-fdebug-prefix-map=$(pwd)=/src` ensures consistent paths, but when the binary is copied locally, GDB resolves paths to the macOS location.

#### launch.json Configuration

**Minimal setupCommands** (VS Code handles symbol loading automatically):

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to client gdb",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/app.remote",
      "MIMode": "gdb",
      "cwd": "${workspaceFolder}",
      "miDebuggerServerAddress": "localhost:7778",
      "miDebuggerPath": "gdb",
      "useExtendedRemote": true,
      "stopAtEntry": true,
      "preLaunchTask": "copy-remote-binary-client",
      "setupCommands": [
        {
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        },
        {
          "text": "set remote exec-file /src/app",
          "ignoreFailures": false
        }
      ]
    }
  ]
}
```

**Important**: 
- **Do NOT use `sourceFileMap`** - GDB sees macOS paths from the local binary copy
- **Do NOT manually load symbols** - VS Code handles this via the `program` field
- **Minimal setupCommands** - Only what's necessary

### Setup Debugging

1. **Port Forward gdbserver**:
   ```bash
   # For server (port 7777)
   POD=$(kubectl get pods -l app=srv --field-selector=status.phase=Running -o jsonpath='{.items[0].metadata.name}')
   kubectl port-forward $POD 7777:7777
   
   # For client (port 7778)
   kubectl port-forward session-reuse-client 7778:7777
   ```
   
   Or use VS Code tasks:
   - Press `Cmd+Shift+P` (macOS) or `Ctrl+Shift+P` (Linux/Windows)
   - Type "Tasks: Run Task"
   - Select "port-forward-gdb" (server) or "port-forward-gdb-client" (client)

2. **Start Debugging**:
   - Open VS Code in the `client/` or `server/` directory
   - **Wait for yellow arrow** at `main()` (due to `stopAtEntry: true`)
   - **Set breakpoints** in editor - they should be **solid red** immediately
   - Press `F5` or click "Start Debugging"
   - Select "Attach to client gdb" or "Attach to server gdb" configuration

### Breakpoint Workflow

**Correct Sequence**:
1. **Start debugging** (F5) - stops at `main()` (yellow arrow)
2. **Set breakpoint** in editor - should be **solid red** (not white/hollow)
3. **Continue** (F5) - breakpoint will hit when code runs

**If breakpoint is white (hollow)**:
- Check what path GDB sees: In DEBUG CONSOLE, run `-exec info sources`
- If GDB shows macOS path (e.g., `/Users/.../main.c`): Remove `sourceFileMap` from `launch.json`
- If GDB shows `/src/main.c`: Verify binary was built with `-fdebug-prefix-map=$(pwd)=/src`

**Alternative: Function breakpoints** (never pending):
```
-exec break do_handshake
```

### Triggering Breakpoints

**For Server**: The server waits in `accept()` for incoming connections. To trigger breakpoints:

```bash
# Make a TLS connection from client pod
kubectl exec session-reuse-client -- openssl s_client -connect session-reuse-server:4433 -quiet 2>&1 <<< "Q"

# Or run the client application
kubectl exec session-reuse-client -- /src/app
```

**For Client**: The client connects immediately when started. Breakpoints will hit automatically.

### Debugging Workflow

1. **Start debugging** (F5) - stops at `main()` (yellow arrow)
2. **Set breakpoints** in editor - should be solid red immediately
3. **Continue** (F5) - execution will stop at breakpoints
4. **Inspect variables** in the Variables panel
5. **Step through code** using F10 (Step Over) or F11 (Step Into)

### Troubleshooting Debugging

**Breakpoints are white (hollow) / PENDING**:
- **Diagnostic**: While stopped at main, run `-exec info sources` in DEBUG CONSOLE
- **If GDB shows macOS path**: Remove `sourceFileMap` from `launch.json` (VS Code should use macOS path directly)
- **If GDB shows `/src/main.c`**: Verify binary was built with `-fdebug-prefix-map=$(pwd)=/src`
- **Solution**: Wait for yellow arrow at main before setting breakpoints, or use function breakpoints: `-exec break function_name`

**Breakpoints not hitting**:
- Verify pod is running: `kubectl get pods`
- Check port forwarding is active: `ps aux | grep port-forward`
- Ensure breakpoint is solid red (not white/hollow)
- For server: Make a connection to trigger `accept()` to return

**Variables not showing**:
- Ensure program is paused at a breakpoint in your code (not in system libraries)
- Select the correct frame in the Call Stack panel (should show your function, not "Unknown")
- Verify remote binary was copied: `ls -la app.remote`

**Connection errors**:
- Verify gdbserver is running in pod: `kubectl exec <pod> -- ps aux | grep gdbserver`
- Check port forwarding: `kubectl port-forward` should be running
- Restart port forwarding if connection is closed

## Debugging OpenSSL Source Code

This project is configured to allow stepping into OpenSSL library functions (e.g., `SSL_shutdown`, `SSL_connect`, `SSL_new`) and viewing the OpenSSL source code during debugging.

### Prerequisites

1. **OpenSSL Source Code**: The OpenSSL source is included as a git submodule.
   
   **If cloning the repository for the first time**:
   ```bash
   git clone --recurse-submodules <repository-url>
   ```
   
   **If you already have the repository cloned**:
   ```bash
   cd debug_test
   git submodule update --init --recursive
   ```
   
   This will clone the OpenSSL repository into the `openssl/` directory.
   
   **Note**: The `.gitmodules` file is already configured. If you need to set up the submodule manually:
   ```bash
   git submodule add https://github.com/openssl/openssl.git openssl
   ```

2. **Dynamic Linking with Debug Symbols**: OpenSSL is built as shared libraries with debug symbols (not statically linked).

### Dockerfile Configuration

Both `server/Dockerfile` and `client/Dockerfile` are configured to:

1. **Build OpenSSL from source with debug symbols (shared libraries)**:
   ```dockerfile
   # Build OpenSSL from source with debug symbols (shared libraries)
   WORKDIR /build/openssl
   COPY openssl .
   RUN ./Configure -d \
       linux-aarch64 \
       --prefix=/usr/local/openssl \
       --openssldir=/usr/local/openssl/ssl \
       && make -j$(nproc) \
       && make install
   ```
   - The `-d` flag enables debug symbols
   - Builds shared libraries (`.so` files) with debug symbols embedded

2. **Dynamically link OpenSSL into the application**:
   ```dockerfile
   # Link dynamically against custom OpenSSL with debug symbols
   RUN gcc -O0 -g -Wall -fdebug-prefix-map=$(pwd)=/src \
       -I/usr/local/openssl/include \
       -L/usr/local/openssl/lib \
       -o app main.c -lssl -lcrypto -lpthread -ldl
   ```
   - Standard dynamic linking (`-lssl -lcrypto`)
   - OpenSSL debug symbols are in the `.so` files
   - GDB automatically loads debug symbols from shared libraries
   - **Benefits**: Much smaller images (5-6GB vs 9GB), same debugging capability

### VS Code launch.json Configuration

The `launch.json` must include `sourceFileMap` **only for OpenSSL paths** (not for `/src`):

```json
{
  "name": "Attach to server gdb",
  "type": "cppdbg",
  "request": "launch",
  "program": "${workspaceFolder}/app.remote",
  "MIMode": "gdb",
  "miDebuggerServerAddress": "localhost:7777",
  "useExtendedRemote": true,
  "sourceFileMap": {
    "/build/openssl": "${workspaceFolder}/../openssl"
  },
  "setupCommands": [
    {
      "text": "-enable-pretty-printing",
      "ignoreFailures": true
    },
    {
      "text": "set remote exec-file /src/app",
      "ignoreFailures": false
    },
    {
      "text": "set substitute-path /build/openssl ${workspaceFolder}/../openssl",
      "ignoreFailures": true
    }
  ]
}
```

**Key Configuration**:
- `sourceFileMap`: Maps `/build/openssl` (container build path) to `${workspaceFolder}/../openssl` (local OpenSSL source)
- **Do NOT include `/src` in `sourceFileMap`** - GDB resolves application source paths naturally when using local `app.remote`
- `set substitute-path`: Additional GDB path substitution for OpenSSL files
- Both server and client use the same configuration

### How It Works

1. **Build Time**: OpenSSL is compiled with debug symbols at `/build/openssl/` in the container as shared libraries
2. **Debug Symbols**: The `.so` files (`libssl.so.4`, `libcrypto.so.4`) contain debug symbols with references to `/build/openssl/ssl/ssl_lib.c`, etc.
3. **Source Mapping**: VS Code maps `/build/openssl` → `${workspaceFolder}/../openssl` (your local clone)
4. **Stepping**: When you step INTO an OpenSSL function, GDB loads debug symbols from the `.so` files and VS Code opens the local source file

### Verifying Dynamic Linking

After building and deploying, verify OpenSSL is dynamically linked:

```bash
# Check if OpenSSL libraries are dynamically linked
kubectl exec <pod-name> -- ldd /src/app | grep ssl
# Should show: libssl.so.4 => /usr/local/openssl/lib/libssl.so.4
#              libcrypto.so.4 => /usr/local/openssl/lib/libcrypto.so.4
```

If you see the libraries pointing to `/usr/local/openssl/lib/`, dynamic linking is working correctly.

### Image Size Benefits

**Dynamic linking provides significant size savings**:
- **Static linking** (old): ~8.96GB images
- **Dynamic linking** (current): ~5-6GB images
- **Savings**: 34-40% smaller images (3-4GB saved)
- **Same debugging capability**: Can still step into OpenSSL source code

### Using OpenSSL Debugging

1. **Set a breakpoint** at an OpenSSL function call:
   ```c
   SSL_shutdown(ssl);  // Set breakpoint here
   ```

2. **Start debugging** (F5) and trigger the breakpoint

3. **Step INTO** (F11) the OpenSSL function:
   - VS Code will open the OpenSSL source file (e.g., `ssl/ssl_lib.c`)
   - You'll see the actual OpenSSL implementation
   - You can inspect OpenSSL internal variables and structures

4. **Navigate OpenSSL code**:
   - Step through OpenSSL functions
   - View OpenSSL call stack
   - Inspect OpenSSL internal state

### Troubleshooting OpenSSL Debugging

**"File not found" when stepping into OpenSSL**:
- Verify OpenSSL source exists: `ls -la ../openssl/ssl/ssl_lib.c`
- Check `sourceFileMap` in `launch.json` points to correct path (should only have `/build/openssl`, not `/src`)
- Ensure OpenSSL was built with `-d` flag in Dockerfile

**Can't step into OpenSSL functions**:
- Verify dynamic linking: `kubectl exec <pod> -- ldd /src/app` should show `libssl.so.4` pointing to `/usr/local/openssl/lib/`
- Check Dockerfile builds OpenSSL as shared libraries (no `no-shared` flag)
- Verify `LD_LIBRARY_PATH` includes `/usr/local/openssl/lib` in container
- Rebuild image if OpenSSL wasn't built with debug symbols

**OpenSSL source file opens but shows wrong code**:
- Ensure local OpenSSL version matches the version built in container
- Check `git log` in `../openssl` to verify version

**Breakpoints not working in application code**:
- **Do NOT include `/src` in `sourceFileMap`** - GDB resolves paths naturally from local `app.remote`
- Remove `/src` mapping if present: `sourceFileMap` should only have `/build/openssl`
- Restart debugging after changing `launch.json`

## Workflow: Updating Images

### Option 1: Automatic Pull from Harbor (Recommended)

Once Harbor is configured, you can update images without using `kind load`:

1. **Build and push new image version**:
   ```bash
   docker build -t harbor.com/library/session-reuse-client:2 .
   docker push harbor.com/library/session-reuse-client:2
   ```

2. **Update the YAML file** with the new tag:
   ```yaml
   image: harbor.com/library/session-reuse-client:2
   ```

3. **Apply the updated manifest**:
   ```bash
   kubectl apply -f client/session_reuse_client.yaml
   ```

   Kubernetes will automatically pull the new image from Harbor!

### Option 2: Load Images Directly (Development/Recommended for Kind)

For kind clusters, loading images directly is often more reliable than pulling from Harbor due to TLS certificate issues:

```bash
# Build the image locally
docker build -t harbor.com/library/session-reuse-server:1 .

# Load into kind cluster
kind load docker-image harbor.com/library/session-reuse-server:1 --name kind

# Update deployment to use locally loaded image
kubectl patch deployment session-reuse-server -p '{"spec":{"template":{"spec":{"containers":[{"name":"srv","imagePullPolicy":"Never"}]}}}}'

# Restart to use new image
kubectl rollout restart deployment/session-reuse-server
```

**Note**: 
- This bypasses Harbor and loads images directly into the cluster nodes
- Use `imagePullPolicy: Never` to ensure Kubernetes uses the locally loaded image
- You must reload images after rebuilding them
- This is the recommended approach for local development with kind clusters

## Image References

All images in this project use Harbor registry:

- **Client**: `harbor.com/library/session-reuse-client:1`
- **Server**: `harbor.com/library/session-reuse-server:1`

The YAML files include `imagePullSecrets` to authenticate with Harbor:
```yaml
spec:
  imagePullSecrets:
  - name: harbor-registry-secret
  containers:
  - name: cli
    image: harbor.com/library/session-reuse-client:1
```

## Troubleshooting

### Pods stuck in `ImagePullBackOff`

**Problem**: Kubernetes can't pull images from Harbor.

**Solutions**:
1. Verify Harbor is running:
   ```bash
   curl -k https://harbor.com/api/v2.0/health
   ```

2. Re-run the setup script:
   ```bash
   ./setup-harbor-kind.sh
   ```

3. Check if the secret exists:
   ```bash
   kubectl get secret harbor-registry-secret
   ```

4. Verify `/etc/hosts` on kind nodes:
   ```bash
   docker exec kind-control-plane cat /etc/hosts | grep harbor
   ```

### Harbor not accessible from kind nodes

**Problem**: Kind nodes can't resolve `harbor.com`.

**Solution**: The gateway IP might have changed. Find the correct IP:
```bash
docker exec kind-control-plane ip route | grep default
# Use the gateway IP (e.g., 172.20.0.1) and update /etc/hosts:
docker exec kind-control-plane sh -c 'echo "172.20.0.1 harbor.com" >> /etc/hosts'
```

### Images not found in Harbor

**Problem**: `404 Not Found` when pulling images.

**Solutions**:
1. Verify images exist in Harbor:
   ```bash
   curl -k -u admin:Harbor12345 \
     "https://harbor.com/api/v2.0/projects/library/repositories"
   ```

2. Check image tags:
   ```bash
   curl -k -u admin:Harbor12345 \
     "https://harbor.com/api/v2.0/projects/library/repositories/session-reuse-client/artifacts"
   ```

3. Re-push the image if needed:
   ```bash
   docker push harbor.com/library/session-reuse-client:1
   ```

### TLS Certificate Error when Pulling from Harbor

**Problem**: Pods stuck in `ErrImagePull` with error:
```
Failed to pull image "harbor.com/library/session-reuse-server:1": 
failed to pull and unpack image "harbor.com/library/session-reuse-server:1": 
failed to resolve reference "harbor.com/library/session-reuse-server:1": 
failed to do request: Head "https://harbor.com/v2/library/session-reuse-server/manifests/1": 
tls: failed to verify certificate: x509: certificate signed by unknown authority
```

**Cause**: Kind nodes cannot verify Harbor's TLS certificate when using `imagePullPolicy: Always`. This happens because Harbor uses a self-signed certificate that kind nodes don't trust.

**Solution**: Load images directly into the kind cluster using `kind load`, bypassing Harbor TLS verification:

1. **Load the image into kind**:
   ```bash
   kind load docker-image harbor.com/library/session-reuse-server:1 --name kind
   kind load docker-image harbor.com/library/session-reuse-client:1 --name kind
   ```

2. **Update deployment to use locally loaded image**:
   ```bash
   kubectl patch deployment session-reuse-server -p '{"spec":{"template":{"spec":{"containers":[{"name":"srv","imagePullPolicy":"Never"}]}}}}'
   ```

3. **Restart the deployment**:
   ```bash
   kubectl rollout restart deployment/session-reuse-server
   ```

**Alternative**: If you need to use Harbor pull, you can configure kind nodes to trust Harbor's certificate, but using `kind load` is simpler for local development.

**Note**: When using `imagePullPolicy: Never`, Kubernetes will only use images that are already loaded in the cluster nodes. You must reload images after rebuilding them.

## Harbor Management

### View Images in Harbor

Access Harbor UI:
- URL: `https://harbor.com`
- Username: `admin`
- Password: `Harbor12345`

Or use the API:
```bash
# List all repositories in library project
curl -k -u admin:Harbor12345 \
  "https://harbor.com/api/v2.0/projects/library/repositories"

# List artifacts (tags) for a repository
curl -k -u admin:Harbor12345 \
  "https://harbor.com/api/v2.0/projects/library/repositories/session-reuse-client/artifacts"
```

### Create New Projects

To use a different project (instead of `library`):

1. Create project in Harbor UI or via API
2. Update image references: `harbor.com/your-project/image:tag`
3. Update the registry secret if needed (project permissions)

## Cleanup

To remove all resources:
```bash
# Delete Kubernetes resources
kubectl delete -f client/session_reuse_client.yaml
kubectl delete -f server/session_reuse_server.yaml

# Delete Harbor secret
kubectl delete secret harbor-registry-secret

# Delete kind cluster
kind delete cluster --name kind
```

## Additional Resources

- [Harbor Documentation](https://goharbor.io/docs/)
- [Kind Documentation](https://kind.sigs.k8s.io/)
- [Kubernetes Image Pull Secrets](https://kubernetes.io/docs/concepts/containers/images/#specifying-imagepullsecrets-on-a-pod)

