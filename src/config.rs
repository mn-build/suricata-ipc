use crate::errors::Error;

use askama::Template;
use log::debug;
use std::io::Write;
use std::path::PathBuf;

pub struct InternalIps(Vec<String>);

impl InternalIps {
    pub fn new(ips: Vec<String>) -> Self {
        InternalIps(ips)
    }
}

impl std::fmt::Display for InternalIps {
    fn fmt(&self, fmt: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let ips = &self.0;
        write!(fmt, "{}", ips.join(","))?;
        Ok(())
    }
}

#[derive(Template)]
#[template(path = "suricata.yaml.in", escape = "none")]
struct ConfigTemplate<'a> {
    rules: &'a str,
    alerts: &'a str,
    suricata_config_path: &'a str,
    internal_ips: &'a InternalIps,
    stats: &'a str,
    max_pending_packets: &'a str,
}

/// Configuration options for suricata
pub struct Config {
    /// Whether statistics should be enabled (output) for suricata
    pub enable_stats: bool,
    /// Path where config will be materialized to
    pub materialize_config_to: PathBuf,
    /// Path where the suricata executable lives
    pub exe_path: PathBuf,
    /// Path where the alert socket should reside at
    pub alert_path: PathBuf,
    /// Path where the rules reside at
    pub rule_path: PathBuf,
    /// Path where suricata config resides at (e.g. threshold config)
    pub suriata_config_path: PathBuf,
    /// Internal ips to use for HOME_NET
    pub internal_ips: InternalIps,
    /// Max pending packets before suricata will block on incoming packets
    pub max_pending_packets: u16,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            enable_stats: false,
            materialize_config_to: PathBuf::from("/etc/suricata/bellini.yaml"),
            exe_path: {
                if let Some(e) = std::env::var_os("SURICATA_EXE").map(|s| PathBuf::from(s)) {
                    e
                } else {
                    PathBuf::from("/usr/local/bin/suricata")
                }
            },
            alert_path: PathBuf::from("/tmp/suricata.alerts"),
            rule_path: PathBuf::from("/etc/suricata/custom.rules"),
            suriata_config_path: PathBuf::from("/etc/suricata"),
            internal_ips: InternalIps(vec![
                String::from("10.0.0.0/8,172.16.0.0/12"),
                String::from("e80:0:0:0:0:0:0:0/64"),
                String::from("127.0.0.1/32"),
                String::from("fc00:0:0:0:0:0:0:0/7"),
                String::from("192.168.0.0/16"),
                String::from("169.254.0.0/16"),
            ]),
            max_pending_packets: 800,
        }
    }
}

impl Config {
    pub fn materialize(&self) -> Result<(), Error> {
        let rules = self.rule_path.to_string_lossy().to_owned();
        let alerts = self.alert_path.to_string_lossy().to_owned();
        let suricata_config_path = self.suriata_config_path.to_string_lossy().to_owned();
        let internal_ips = &self.internal_ips;
        let stats = format!("{}", self.enable_stats);
        let max_pending_packets = format!("{}", self.max_pending_packets);
        let template = ConfigTemplate {
            rules: &rules,
            alerts: &alerts,
            suricata_config_path: &suricata_config_path,
            internal_ips: internal_ips,
            stats: &stats,
            max_pending_packets: &max_pending_packets,
        };
        debug!("Attempting to render");
        let rendered = template.render().map_err(Error::Askama)?;
        debug!("Writing output.yaml to {:?}", self.materialize_config_to);
        let mut f = std::fs::File::create(&self.materialize_config_to).map_err(Error::Io)?;
        f.write(rendered.as_bytes()).map_err(Error::Io)?;
        debug!("Output file written");
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use crate::config::InternalIps;

    #[test]
    fn test_internal_ip_display() {
        let internal_ips = InternalIps(vec![
            "169.254.0.0/16".to_owned(),
            "192.168.0.0/16".to_owned(),
            "fc00:0:0:0:0:0:0:0/7".to_owned(),
            "127.0.0.1/32".to_owned(),
            "10.0.0.0/8".to_owned(),
            "172.16.0.0/12".to_owned(),
        ]);
        assert_eq!(format!("{}", internal_ips), "169.254.0.0/16,192.168.0.0/16,fc00:0:0:0:0:0:0:0/7,127.0.0.1/32,10.0.0.0/8,172.16.0.0/12");
    }
}
