# Toyium OS — /root/.profile
[ -f /etc/profile ] && . /etc/profile
# per-user tweaks
export PS1='\[\e[1;31m\]root@toyium\[\e[0m\]:\[\e[1;34m\]\w\[\e[0m\]# '
mesg n 2>/dev/null || true
