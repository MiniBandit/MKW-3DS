# Running the online server

The server (`tools/server/mkw3ds_server.py`) is one Python file with no
dependencies. Consoles connect out to it, so players need no router set-up. It
only introduces players and passes their race packets along, about 6 KB a second
per player, so a free cloud machine is plenty.

## 1. Get a free server

Any always-on Linux machine with a public address works. One free option:

**Oracle Cloud "Always Free"** (oracle.com/cloud/free). Sign-up asks for a card
to check identity; Always Free machines are not charged.

1. Create the account and sign in.
2. **Compute → Instances → Create instance.** Image: **Ubuntu**. Shape: one marked
   *Always Free-eligible*. Under *Add SSH keys*, download the private key.
3. Note the instance's **public IP address**.
4. Open the game's port in Oracle's firewall: open the instance's **subnet → Security
   list → Add ingress rule**: source `0.0.0.0/0`, protocol **UDP**, destination port
   **41750**.

## 2. Install the server

Connect from a terminal (Mac: Terminal; Windows: PowerShell), using the key you
downloaded and the public IP:

```bash
ssh -i path/to/the-key.key ubuntu@PUBLIC_IP
```

Then, on the server:

```bash
sudo apt update && sudo apt install -y python3
sudo mkdir -p /opt/mkw3ds && sudo chown ubuntu /opt/mkw3ds
```

Copy the server file up from your computer (in a second terminal, in the MKW-3DS
folder):

```bash
scp -i path/to/the-key.key tools/server/mkw3ds_server.py ubuntu@PUBLIC_IP:/opt/mkw3ds/
```

Ubuntu on Oracle also blocks ports inside the machine. Back on the server, open
UDP 41750 there too and keep the rule after restarts:

```bash
sudo iptables -I INPUT -p udp --dport 41750 -j ACCEPT
sudo apt install -y iptables-persistent && sudo netfilter-persistent save
```

## 3. Allow your build

The server only accepts builds whose fingerprint is listed. On your computer, in
the MKW-3DS folder, print the fingerprint of the build you're handing out:

```bash
python3 tools/build_id.py
```

On the server, put it in the allowed list (add one line per build you accept;
`#` starts a comment). The server notices changes by itself.

```bash
echo "PASTE_THE_FINGERPRINT  # 0.8.0 test" > /opt/mkw3ds/allowed-builds.txt
```

Every build made from the same source has the same fingerprint, on any computer.
A changed game gets a different one and is told to download the official build.
This stops other builds and casual edits; it can't stop someone determined to
fake it, since the game is open source.

## 4. Keep it running

```bash
sudo tee /etc/systemd/system/mkw3ds.service >/dev/null <<'EOF'
[Unit]
Description=MKW 3DS online relay
After=network-online.target

[Service]
User=ubuntu
ExecStart=/usr/bin/python3 /opt/mkw3ds/mkw3ds_server.py --port 41750 --allowed /opt/mkw3ds/allowed-builds.txt
Restart=always

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl enable --now mkw3ds
```

Check it, and watch players join (Ctrl+C stops watching, not the server):

```bash
systemctl status mkw3ds
journalctl -u mkw3ds -f
```

## 5. Point consoles at it

Each tester puts a file named `lan.txt` in `sd:/3ds/mkw-3ds/` containing:

```
online PUBLIC_IP
```

See [ONLINE-TESTING.md](ONLINE-TESTING.md) for what testers do next.

## Updating

To accept a new build, add its fingerprint to `allowed-builds.txt`. To update the
server program, copy the new `mkw3ds_server.py` up and run
`sudo systemctl restart mkw3ds`.
