#!/bin/bash
# Setup script to make Harbor accessible from kind cluster
# Run this after creating your kind cluster

set -e

GATEWAY_IP="172.20.0.1"
NODES=$(docker ps --filter "name=kind" --format "{{.Names}}")

if [ -z "$NODES" ]; then
  echo "No kind nodes found. Make sure your kind cluster is running."
  exit 1
fi

echo "Setting up Harbor access for kind cluster..."
for node in $NODES; do
  echo "  Updating /etc/hosts on $node"
  docker exec $node sh -c "grep -q harbor.com /etc/hosts || echo '$GATEWAY_IP harbor.com' >> /etc/hosts"
done

echo "Creating Harbor registry secret..."
kubectl create secret docker-registry harbor-registry-secret \
  --docker-server=harbor.com \
  --docker-username=admin \
  --docker-password=Harbor12345 \
  --docker-email=admin@harbor.com \
  --dry-run=client -o yaml | kubectl apply -f -

echo ""
echo "✓ Harbor setup complete!"
echo "  - harbor.com is accessible from all kind nodes"
echo "  - Image pull secret 'harbor-registry-secret' is created"
echo ""
echo "Now you can push new images to Harbor and update your deployments"
echo "without needing to run 'kind load docker-image'!"

