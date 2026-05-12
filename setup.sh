echo "installing deps..."

sudo apt update
sudo apt install -y \
    gnu-efi \
    build-essential \
    gcc \
    binutils \
    qemu-system-x86 \
    ovmf \
    mtools \
    dosfstools \
    git

echo ""
echo "checking installs..."
echo "GCC: $(gcc --version | head -1)"
echo "GNU-EFI: $([ -f /usr/include/efi/efi.h ] && echo 'Found' || echo 'NOT FOUND')"
echo "QEMU: $(qemu-system-x86_64 --version | head -1)"
echo "OVMF: $([ -f /usr/share/ovmf/OVMF.fd ] && echo 'Found' || echo 'NOT FOUND')"
echo ""
echo "deps installed"
