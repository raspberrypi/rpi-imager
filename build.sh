#!/usr/bin/env bash


reset_db_root_pass() {
sudo mkdir /var/run/mysqld; sudo chown mysql /var/run/mysqld
sudo mysqld_safe --skip-grant-tables&
}

purge_mysql_install() {
apt purge -y mysql-server mysql-client mysql-common
apt autoremove -y
sudo rm -rf /var/lib/mysql*
sudo rm /etc/mysql/debian.cnf
}

version_change() {
update-ca-certificates
}


setup_cloudstack_db() {

# from https://docs.cloudstack.apache.org/en/latest/installguide/management-server/#install-the-database-server
# set root password: 
# sudo mysqld_safe --skip-grant-tables&
# sudo mysql --user=root mysql
# alter user 'root'@'localhost' identified by '4rtaxs';

cloudstack-setup-databases cloud:'cloud'@localhost --deploy-as=root: -i 127.0.0.1

cat << HereDoc > setup_cloudstack_db.sql
#CREATE DATABASE cloud;
#CREATE DATABASE cloud_usage;
#CREATE USER 'cloud'@'localhost' identified by '4rtaxs';
#CREATE USER 'cloud'@'%' identified by '4rtaxs';
GRANT ALL ON cloud.* to cloud@'localhost';
GRANT ALL ON cloud.* to cloud@'%';
GRANT ALL ON cloud_usage.* to cloud@'localhost';
GRANT ALL ON cloud_usage.* to cloud@'%';
GRANT process ON *.* TO cloud@'localhost';
GRANT process ON *.* TO cloud@'%';
HereDoc

mysql -u root -p '' -h localhost <setup_cloudstack_db.sql
}


# ensure we're running via sudo
if [ $(whoami) = "root" ]
then
echo "executing as $(whoami)"

else
echo "error: must run \"sudo $0\""
exit
fi


# is this a raspberry pi 5?
if [ $(uname -r) = "6.11.0-1006-raspi" ]
        then
                echo "OS version match"
        else
                echo "OS version mismatch"
                exit
fi

echo "****** apt update ******"
apt-get update -y
echo "****** apt upgrade ******"
apt-get upgrade -y
echo "****** apt install ******"
apt-get install -y ntpdate sudo vim htop tar iotop bridge-utils cpu-checker mysql-server

ntpdate time.nist.gov

update_mysqld_cnf() {
echo "****** update_mysqld_cnf ******"

cat <<HereDoc >> /etc/mysql/mysql.conf.d/mysqld.cnf
#server_id = 1
#sql-mode="STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION,ERROR_FOR_DIVISION_BY_ZERO,NO_ZERO_DATE,NO_ZERO_IN_DATE,NO_ENGINE_SUBSTITUTION"
#innodb_rollback_on_timeout=1
#innodb_lock_wait_timeout=600
#max_connections=1000
#log-bin=mysql-bin
#binlog-format = 'ROW'

#default-authentication-plugin=mysql_native_password
HereDoc
}

update_mysql_cnf() {
echo "****** update_mysql_cnf ******"

cat <<HereDoc >> /etc/mysql/mysql.conf.d/mysql.cnf
#server_id = 1
#sql-mode="STRICT_TRANS_TABLES,NO_ENGINE_SUBSTITUTION,ERROR_FOR_DIVISION_BY_ZERO,NO_ZERO_DATE,NO_ZERO_IN_DATE,NO_ENGINE_SUBSTITUTION"
#innodb_rollback_on_timeout=1
#innodb_lock_wait_timeout=600
#max_connections=1000
#log-bin=mysql-bin
#binlog-format = 'ROW'

#default-authentication-plugin=mysql_native_password
HereDoc
}

update_mysql_root() {
USE mysql
SELECT User, Host, plugin FROM mysql.user;
UPDATE user SET plugin='mysql_native_password' WHERE User='root';
COMMIT;
ALTER USER 'root'@'localhost' IDENTIFIED BY '4rtaxs';
}

if [ $(grep "ERROR_FOR_DIVISION_BY_ZERO" /etc/mysql/mysql.conf.d/mysqld.cnf) ]
then
        echo "mysqld.cnf already updated"
        return
else
        echo "updating mysqld.cnf"
        update_mysql_cnf
        update_mysqld_cnf
fi

echo "****** restart mysql ******"
systemctl restart mysql


# Setup Repo
echo "****** setup apt repo ******"
mkdir -p /etc/apt/keyrings
wget -O- http://packages.shapeblue.com/release.asc | gpg --dearmor | sudo tee /etc/apt/keyrings/cloudstack.gpg > /dev/null
echo deb [signed-by=/etc/apt/keyrings/cloudstack.gpg] http://packages.shapeblue.com/cloudstack/upstream/debian/4.20 / > /etc/apt/sources.list.d/cloudstack.list

# Install management server
echo "****** install cloudstack ******"
apt-get update
apt-get install -y cloudstack-management cloudstack-usage

# Stop the automatic start after install
systemctl stop cloudstack-management cloudstack-usage

echo "****** setup cloudstack database ******"
setup_cloudstack_db

echo "****** kvm setup ******"
if [ $(grep "KVM_SETUP_SCRIPT" /etc/libvirt/qemu.conf) ]
then
        echo "kvm configs already updated"
        return
else
        echo "updating kvm configs"
        update_kvm_configs
fi

update_kvm_configs() {
echo '#KVM_SETUP_SCRIPT"' >> /etc/libvirt/qemu.conf
sed -i -e 's/\#vnc_listen.*$/vnc_listen = "0.0.0.0"/g' /etc/libvirt/qemu.conf
echo 'security_driver = "none"' >> /etc/libvirt/qemu.conf
echo LIBVIRTD_ARGS=\"--listen\" >> /etc/default/libvirtd
echo 'listen_tls=0' >> /etc/libvirt/libvirtd.conf
echo 'listen_tcp=1' >> /etc/libvirt/libvirtd.conf
echo 'tcp_port = "16509"' >> /etc/libvirt/libvirtd.conf
echo 'mdns_adv = 0' >> /etc/libvirt/libvirtd.conf
echo 'auth_tcp = "none"' >> /etc/libvirt/libvirtd.conf
}

apt-get install -y qemu-kvm cloudstack-agent
systemctl stop cloudstack-agent

echo "****** configure iptables ******"
echo "doing nothing"

echo "****** mysql defaults ******"
cat /etc/mysql/debian.cnf

echo "****** mask libvirt ******"
systemctl mask libvirtd.socket libvirtd-ro.socket libvirtd-admin.socket libvirtd-tls.socket libvirtd-tcp.socket

# exit sudo
exit


echo "****** start cloudstack ******"
cloudstack-setup-management
systemctl status cloudstack-management
tail -f /var/log/cloudstack/management/management-server.log

