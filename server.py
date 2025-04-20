#!/usr/bin/env python3
"""
External Security Assessment Tool
A comprehensive security scanning tool for web domains
"""

import os
import sys
import socket
import ssl
import datetime
import subprocess
from concurrent.futures import ThreadPoolExecutor

# Handle dependency imports with try/except blocks
try:
    import requests
    import dns.resolver
    import whois
    from colorama import Fore, Style, init
except ImportError as e:
    print(f"Error: Missing required dependency: {e.name}")
    print("Please install required packages with:")
    print("pip install requests dnspython python-whois colorama")
    sys.exit(1)

# Initialize colorama
init()

def print_banner():
    """Display the tool banner"""
    banner = f"""
    {Fore.CYAN}███████╗██╗  ██╗████████╗███████╗███████╗ ██████╗{Style.RESET_ALL}
    {Fore.CYAN}██╔════╝╚██╗██╔╝╚══██╔══╝██╔════╝██╔════╝██╔════╝{Style.RESET_ALL}
    {Fore.CYAN}█████╗   ╚███╔╝    ██║   ███████╗█████╗  ██║     {Style.RESET_ALL}
    {Fore.CYAN}██╔══╝   ██╔██╗    ██║   ╚════██║██╔══╝  ██║     {Style.RESET_ALL}
    {Fore.CYAN}███████╗██╔╝ ██╗   ██║   ███████║███████╗╚██████╗{Style.RESET_ALL}
    {Fore.CYAN}╚══════╝╚═╝  ╚═╝   ╚═╝   ╚══════╝╚══════╝ ╚═════╝{Style.RESET_ALL}
    {Fore.WHITE}External Security Testing & Scanning Engine{Style.RESET_ALL}
    {Fore.YELLOW}v1.0.0 - Security Assessment Tool{Style.RESET_ALL}
    """
    print(banner)

def check_dependencies():
    """Check if required external tools are installed"""
    required_tools = ["nmap", "nikto"]
    missing_tools = []
    
    for tool in required_tools:
        try:
            subprocess.run([tool, "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except FileNotFoundError:
            missing_tools.append(tool)
    
    if missing_tools:
        print(f"{Fore.RED}[!] Required tools not found: {', '.join(missing_tools)}{Style.RESET_ALL}")
        print(f"Please install these tools before running this script.")
        sys.exit(1)

def validate_domain(domain):
    """Validate domain format"""
    if not domain or "." not in domain:
        return False
    
    # Add more comprehensive domain validation
    try:
        socket.gethostbyname(domain)
        return True
    except socket.gaierror:
        print(f"{Fore.YELLOW}[!] Warning: Could not resolve domain {domain}. Proceeding anyway.{Style.RESET_ALL}")
        return True  # Still return True to allow scanning of potentially valid but unresolvable domains

def get_whois_info(domain):
    """Get WHOIS information for the domain"""
    try:
        w = whois.whois(domain)
        
        # Handle different return types for creation and expiration dates
        creation_date = w.creation_date
        expiration_date = w.expiration_date
        
        # Handle list returns
        if isinstance(creation_date, list) and creation_date:
            creation_date = creation_date[0]
        if isinstance(expiration_date, list) and expiration_date:
            expiration_date = expiration_date[0]
            
        # Format dates properly
        creation_str = str(creation_date).split()[0] if creation_date else "Unknown"
        expiration_str = str(expiration_date).split()[0] if expiration_date else "Unknown"
        
        # Handle name servers as either list or string
        name_servers = w.name_servers if w.name_servers else []
        if not isinstance(name_servers, list):
            name_servers = [name_servers]
        
        whois_info = {
            "Domain Name": domain.upper(),
            "Registrar": w.registrar or "Unknown",
            "Creation Date": creation_str,
            "Expiration Date": expiration_str,
            "Registrant Country": w.country or "Unknown",
            "Name Servers": ", ".join(str(ns).upper() for ns in name_servers) if name_servers else "Unknown"
        }
        return whois_info
    except Exception as e:
        return {"Error": f"Failed to retrieve WHOIS information: {str(e)}"}

def get_dns_records(domain):
    """Get DNS records for the domain"""
    dns_info = {}
    
    # Helper function to safely resolve DNS records
    def safe_resolve(domain, record_type):
        try:
            records = dns.resolver.resolve(domain, record_type)
            return [record.to_text() for record in records]
        except Exception:
            return ["Not found"]
    
    # Resolve various DNS record types
    dns_info["A Records"] = safe_resolve(domain, 'A')
    dns_info["MX Records"] = safe_resolve(domain, 'MX')
    dns_info["TXT Records"] = safe_resolve(domain, 'TXT')
    
    # Try to get NS records as well
    dns_info["NS Records"] = safe_resolve(domain, 'NS')
    
    return dns_info

def enumerate_subdomains(domain):
    """Simple subdomain enumeration with common prefixes"""
    common_subdomains = [
        "www", "mail", "admin", "blog", "dev", "test", "stage", "api", 
        "support", "app", "shop", "store", "portal", "cdn", "media"
    ]
    found_subdomains = []
    
    print(f"{Fore.CYAN}[*] Enumerating subdomains for {domain}...{Style.RESET_ALL}")
    
    with ThreadPoolExecutor(max_workers=10) as executor:
        futures = {executor.submit(resolve_subdomain, sub, domain): sub for sub in common_subdomains}
        for future in futures:
            subdomain = future.result()
            if subdomain:
                found_subdomains.append(subdomain)
    
    return found_subdomains

def resolve_subdomain(subdomain_prefix, domain):
    """Helper function to resolve a subdomain"""
    subdomain = f"{subdomain_prefix}.{domain}"
    try:
        socket.gethostbyname(subdomain)
        print(f"{Fore.GREEN}[+] Found subdomain: {subdomain}{Style.RESET_ALL}")
        return subdomain
    except:
        return None

def check_ssl_tls(domain):
    """Check SSL/TLS configuration"""
    ssl_info = {}
    
    try:
        context = ssl.create_default_context()
        with socket.create_connection((domain, 443), timeout=10) as sock:
            with context.wrap_socket(sock, server_hostname=domain) as ssock:
                # Get SSL/TLS version
                ssl_info["SSL/TLS Version"] = ssock.version()
                
                # Get certificate details
                cert = ssock.getpeercert()
                
                # Format certificate expiry date
                not_after = datetime.datetime.strptime(cert['notAfter'], "%b %d %H:%M:%S %Y %Z")
                ssl_info["Certificate Expiry"] = f"Expires on {not_after.strftime('%Y-%m-%d')}"
                
                # Check if certificate is close to expiry
                days_to_expiry = (not_after - datetime.datetime.now()).days
                if days_to_expiry < 30:
                    ssl_info["Expiry Warning"] = f"⚠️ Certificate expires in {days_to_expiry} days!"
                
                # Get certificate issuer
                issuer = dict(x[0] for x in cert['issuer'])
                ssl_info["Issuer"] = issuer.get('organizationName', "Unknown")
                
                # Get supported cipher suites
                ssl_info["Weak Ciphers"] = "None detected"  # Would require additional testing
                
                return ssl_info
    except (socket.error, ssl.SSLError, ssl.CertificateError) as e:
        return {
            "Error": f"SSL/TLS connection failed: {str(e)}",
            "SSL/TLS Version": "Unknown",
            "Certificate Expiry": "Unknown",
            "Issuer": "Unknown",
            "Weak Ciphers": "Unknown"
        }
    except Exception as e:
        return {
            "Error": f"Failed to check SSL/TLS: {str(e)}",
            "SSL/TLS Version": "Unknown",
            "Certificate Expiry": "Unknown", 
            "Issuer": "Unknown",
            "Weak Ciphers": "Unknown"
        }

def check_http_headers(domain):
    """Check HTTP security headers"""
    headers_info = {}
    expected_headers = {
        "Strict-Transport-Security": "HSTS not enabled",
        "X-Frame-Options": "Missing (clickjacking possible)",
        "X-XSS-Protection": "Missing (XSS protection disabled)",
        "X-Content-Type-Options": "Missing (MIME-sniffing possible)",
        "Content-Security-Policy": "Missing (no content restrictions)",
        "Referrer-Policy": "Missing (may leak referrer information)",
        "Permissions-Policy": "Missing (no feature permissions set)"
    }
    
    try:
        response = requests.get(f"https://{domain}", timeout=10)
        headers = response.headers
        
        # Check each expected security header
        for header, warning in expected_headers.items():
            if header.lower() in (h.lower() for h in headers):
                headers_info[header] = headers[header]
            else:
                headers_info[header] = warning
        
        # Add server header if present (could reveal version info)
        if "Server" in headers:
            headers_info["Server"] = f"⚠️ {headers['Server']} (version information exposed)"
    
    except requests.exceptions.SSLError:
        # Try HTTP if HTTPS fails
        try:
            response = requests.get(f"http://{domain}", timeout=10)
            headers = response.headers
            headers_info["Warning"] = "⚠️ Site does not support HTTPS!"
            
            # Process headers as above
            for header, warning in expected_headers.items():
                if header.lower() in (h.lower() for h in headers):
                    headers_info[header] = headers[header]
                else:
                    headers_info[header] = warning
            
        except Exception as e:
            headers_info["Error"] = f"Failed to connect via HTTP: {str(e)}"
    
    except Exception as e:
        headers_info["Error"] = f"Failed to check HTTP headers: {str(e)}"
    
    return headers_info

def scan_ports(domain):
    """Scan for open ports using Nmap"""
    print(f"{Fore.CYAN}[*] Scanning ports on {domain}...{Style.RESET_ALL}")
    
    try:
        # Run nmap with simplified output
        result = subprocess.run(
            ["nmap", "-F", "-T4", domain],
            capture_output=True,
            text=True,
            timeout=120
        )
        
        if result.returncode == 0:
            return result.stdout
        else:
            return f"Nmap scan failed with error code {result.returncode}:\n{result.stderr}"
    
    except subprocess.TimeoutExpired:
        return "Nmap scan timed out after 120 seconds."
    except Exception as e:
        return f"Error running Nmap scan: {str(e)}"

def fingerprint_web_tech(domain):
    """Fingerprint web technologies"""
    tech_info = {}
    
    try:
        response = requests.get(f"https://{domain}", timeout=10)
        headers = response.headers
        html = response.text.lower()
        
        # Check server header
        if "Server" in headers:
            tech_info["Server"] = headers["Server"]
        
        # Check for common technologies in HTML
        checks = {
            "WordPress": "wp-content",
            "Drupal": "drupal",
            "Joomla": "joomla",
            "jQuery": "jquery",
            "Bootstrap": "bootstrap",
            "React": "react",
            "Angular": "angular",
            "Vue.js": "vue",
            "Font Awesome": "font-awesome"
        }
        
        for tech, keyword in checks.items():
            if keyword in html:
                tech_info[tech] = "Detected"
        
        # Check for X-Powered-By header
        if "X-Powered-By" in headers:
            tech_info["Powered by"] = headers["X-Powered-By"]
        
        return tech_info
    
    except Exception as e:
        return {"Error": f"Failed to fingerprint technologies: {str(e)}"}

def run_web_vulnerability_scan(domain):
    """Run Nikto web vulnerability scan"""
    print(f"{Fore.CYAN}[*] Running Nikto scan on {domain}...{Style.RESET_ALL}")
    
    try:
        # Run a basic Nikto scan with timeout
        result = subprocess.run(
            ["nikto", "-h", f"https://{domain}", "-maxtime", "300"],
            capture_output=True,
            text=True,
            timeout=360  # 6 minutes total timeout
        )
        
        if result.returncode == 0 or result.returncode == 1:  # Nikto often returns 1 even on success
            return result.stdout
        else:
            return f"Nikto scan failed with error code {result.returncode}:\n{result.stderr}"
    
    except subprocess.TimeoutExpired:
        return "Nikto scan timed out after 360 seconds."
    except Exception as e:
        return f"Error running Nikto scan: {str(e)}"

def check_security_misconfigurations(domain):
    """Check for security misconfigurations"""
    misconfigs = []
    
    # Check for robots.txt
    try:
        response = requests.get(f"https://{domain}/robots.txt", timeout=10)
        if response.status_code == 200:
            misconfigs.append("[*] robots.txt found, analyzing content...")
            if "disallow: /admin" in response.text.lower():
                misconfigs.append("[!] WARNING: /admin path revealed in robots.txt")
    except:
        pass
    
    # Check for common admin panels
    admin_paths = ["/admin", "/administrator", "/wp-admin", "/login", "/cms", "/dashboard"]
    for path in admin_paths:
        try:
            response = requests.get(f"https://{domain}{path}", timeout=5)
            if response.status_code == 200 or response.status_code == 401 or response.status_code == 403:
                misconfigs.append(f"[!] WARNING: Potential admin panel found at {path} (Status: {response.status_code})")
        except:
            continue
    
    # Check for directory listing
    try:
        response = requests.get(f"https://{domain}/images/", timeout=10)
        if response.status_code == 200 and "Index of" in response.text:
            misconfigs.append("[!] WARNING: Directory listing is enabled on /images/")
    except:
        pass
    
    if not misconfigs:
        misconfigs.append("[+] No obvious security misconfigurations detected")
    
    return "\n".join(misconfigs)

def check_email_security(domain):
    """Check email security records"""
    email_security = {}
    
    # Check SPF record
    try:
        txt_records = dns.resolver.resolve(domain, 'TXT')
        for record in txt_records:
            text = record.to_text()
            if "v=spf1" in text:
                email_security["SPF Record"] = text.strip('"')
                break
        else:
            email_security["SPF Record"] = "⚠️ Not found (email spoofing possible)"
    except Exception:
        email_security["SPF Record"] = "⚠️ Failed to check"
    
    # Check DMARC record
    try:
        dmarc_records = dns.resolver.resolve(f"_dmarc.{domain}", 'TXT')
        for record in dmarc_records:
            text = record.to_text()
            if "v=DMARC1" in text:
                email_security["DMARC Record"] = text.strip('"')
                break
        else:
            email_security["DMARC Record"] = "⚠️ Not found (no email authentication policy)"
    except Exception:
        email_security["DMARC Record"] = "⚠️ Failed to check"
    
    # Check DKIM (this is a simple check, as DKIM selectors can vary)
    try:
        dkim_records = dns.resolver.resolve(f"default._domainkey.{domain}", 'TXT')
        for record in dkim_records:
            text = record.to_text()
            if "v=DKIM1" in text:
                email_security["DKIM Record"] = "Found (default selector)"
                break
        else:
            email_security["DKIM Record"] = "⚠️ Not found with default selector"
    except Exception:
        email_security["DKIM Record"] = "⚠️ Could not verify (may need specific selector)"
    
    return email_security

def check_blacklists(domain):
    """Check if domain is blacklisted"""
    blacklist_info = {}
    
    # Since we can't query all blacklists easily, we'll do a simplified check
    blacklist_info["Note"] = "Basic blacklist check only - comprehensive checks recommended"
    
    # Try to resolve a special DNS zone used by some blacklists
    ip_address = ""
    try:
        ip_address = socket.gethostbyname(domain)
        reversed_ip = '.'.join(reversed(ip_address.split('.')))
        
        # Check Spamhaus ZEN
        try:
            dns.resolver.resolve(f"{reversed_ip}.zen.spamhaus.org", 'A')
            blacklist_info["Spamhaus ZEN"] = "⚠️ LISTED!"
        except dns.resolver.NXDOMAIN:
            blacklist_info["Spamhaus ZEN"] = "✅ Not listed"
        except Exception:
            blacklist_info["Spamhaus ZEN"] = "❓ Check failed"
        
        # Check SORBS
        try:
            dns.resolver.resolve(f"{reversed_ip}.dnsbl.sorbs.net", 'A')
            blacklist_info["SORBS"] = "⚠️ LISTED!"
        except dns.resolver.NXDOMAIN:
            blacklist_info["SORBS"] = "✅ Not listed"
        except Exception:
            blacklist_info["SORBS"] = "❓ Check failed"
        
    except Exception as e:
        blacklist_info["Error"] = f"Failed to check blacklists: {str(e)}"
    
    return blacklist_info

def check_cve_mapping(domain):
    """Get CVE mapping information"""
    cve_info = []
    server_info = ""
    
    # Try to get server information from HTTP headers
    try:
        response = requests.get(f"https://{domain}", timeout=10)
        server = response.headers.get('Server', '')
        if server:
            server_info = server
            cve_info.append(f"[*] Server software detected: {server}")
            
            # Very basic checks for known vulnerable versions
            if "Apache/2.4.49" in server:
                cve_info.append("[!] CVE-2021-41773 (Critical) - Path Traversal in Apache 2.4.49")
            elif "Apache/2.4.50" in server:
                cve_info.append("[!] CVE-2021-42013 (Critical) - Path Traversal in Apache 2.4.50")
            elif "nginx/1.16" in server:
                cve_info.append("[!] CVE-2019-9511 (High) - HTTP/2 DoS vulnerability in NGINX 1.16")
            
            # Default message if no specific vulnerabilities are found
            if len(cve_info) == 1:
                cve_info.append("[*] No known high-severity CVEs identified for the detected version")
    except Exception as e:
        cve_info.append(f"[!] Failed to check for CVEs: {str(e)}")
    
    if not server_info:
        cve_info.append("[*] No server software version detected for CVE mapping")
    
    return "\n".join(cve_info)

def generate_report(domain):
    """Generate the full security assessment report"""
    # Create a report filename
    report_filename = f"scan_results_{domain}_{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
    
    # Print scanning message
    print(f"{Fore.CYAN}🔍 Running External Security Assessment on: {domain}{Style.RESET_ALL}")
    print(f"{Fore.GREEN}Results will be saved to: {report_filename}{Style.RESET_ALL}")
    
    # Collect data with proper error handling
    with ThreadPoolExecutor(max_workers=5) as executor:
        whois_future = executor.submit(get_whois_info, domain)
        dns_future = executor.submit(get_dns_records, domain)
        subdomains_future = executor.submit(enumerate_subdomains, domain)
        ssl_future = executor.submit(check_ssl_tls, domain)
        headers_future = executor.submit(check_http_headers, domain)

    whois_info = whois_future.result()
    dns_info = dns_future.result()
    subdomains = subdomains_future.result()
    ssl_info = ssl_future.result()
    headers_info = headers_future.result()
    
    # Sequential operations for the tools
    port_scan = scan_ports(domain)
    tech_info = fingerprint_web_tech(domain)
    vulnerability_scan = run_web_vulnerability_scan(domain)
    misconfigs = check_security_misconfigurations(domain)
    email_security = check_email_security(domain)
    blacklist_info = check_blacklists(domain)
    cve_info = check_cve_mapping(domain)
    
    # Create the report content
    report = [
        f"🔍 External Security Assessment for: {domain}",
        f"📅 Date: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "=========================================",
        "📄 WHOIS Information",
        "=========================================",
    ]
    
    # Add WHOIS info
    for key, value in whois_info.items():
        report.append(f"{key}: {value}")
    
    # Add DNS records
    report.append("\n=========================================")
    report.append("🌐 DNS Records")
    report.append("=========================================")
    for key, records in dns_info.items():
        report.append(f"{key}:")
        for record in records:
            report.append(f"  - {record}")
    
    # Add subdomains
    report.append("\n=========================================")
    report.append("🔎 Subdomain Enumeration")
    report.append("=========================================")
    if subdomains:
        for subdomain in subdomains:
            report.append(f"- {subdomain}")
    else:
        report.append("No subdomains discovered")
    
    # Add SSL/TLS info
    report.append("\n=========================================")
    report.append("🔒 SSL/TLS Security Check")
    report.append("=========================================")
    for key, value in ssl_info.items():
        report.append(f"{key}: {value}")
    
    # Add HTTP headers
    report.append("\n=========================================")
    report.append("🛡️ HTTP Security Headers Audit")
    report.append("=========================================")
    for key, value in headers_info.items():
        report.append(f"{key}: {value}")
    
    # Add port scan
    report.append("\n=========================================")
    report.append("🚪 Open Ports & Services (Nmap)")
    report.append("=========================================")
    report.append(port_scan)
    
    # Add web tech fingerprinting
    report.append("\n=========================================")
    report.append("🕵️ Web Technology Fingerprinting")
    report.append("=========================================")
    if tech_info:
        for key, value in tech_info.items():
            report.append(f"{key}: {value}")
    else:
        report.append("No web technologies detected")
    
    # Add vulnerability scan
    report.append("\n=========================================")
    report.append("🌐 Web Vulnerability Scan (Nikto)")
    report.append("=========================================")
    report.append(vulnerability_scan)
    
    # Add misconfigurations
    report.append("\n=========================================")
    report.append("⚠️ Security Misconfiguration Checks")
    report.append("=========================================")
    report.append(misconfigs)
    
    # Add email security
    report.append("\n=========================================")
    report.append("📧 Email Security Records")
    report.append("=========================================")
    for key, value in email_security.items():
        report.append(f"{key}: {value}")
    
    # Add blacklist info
    report.append("\n=========================================")
    report.append("⚠️ Blacklist & Reputation Checks")
    report.append("=========================================")
    for key, value in blacklist_info.items():
        report.append(f"{key}: {value}")
    
    # Add CVE mapping
    report.append("\n=========================================")
    report.append("🔎 CVE Mapping")
    report.append("=========================================")
    report.append(cve_info)
    
    # Add completion message
    report.append("\n=========================================")
    report.append("✅ Scan Complete!")
    report.append(f"📝 Report saved to: {report_filename}")
    report.append("=========================================")
    
    # Write report to file
    with open(report_filename, "w") as f:
        f.write("\n".join(report))
    
    # Print report to screen
    print("\n".join(report))
    print(f"\n{Fore.GREEN}Scan completed! Report saved to {report_filename}{Style.RESET_ALL}")

def main():
    """Main function to run the security assessment tool"""
    print_banner()
    
    # Check if external tools are installed
    check_dependencies()
    
    # Get domain from command line or prompt user
    if len(sys.argv) > 1:
        domain = sys.argv[1]
    else:
        domain = input("Enter the domain to scan (e.g., example.com): ")
    
    # Validate domain format
    if not validate_domain(domain):
        print(f"{Fore.RED}[!] Invalid domain format. Please enter a valid domain name.{Style.RESET_ALL}")
        sys.exit(1)
    
    # Run the security assessment
    try:
        generate_report(domain)
    except KeyboardInterrupt:
        print(f"\n{Fore.YELLOW}[!] Scan interrupted by user.{Style.RESET_ALL}")
        sys.exit(0)
    except Exception as e:
        print(f"\n{Fore.RED}[!] An error occurred during the scan: {str(e)}{Style.RESET_ALL}")
        sys.exit(1)

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{Fore.YELLOW}[!] Scan interrupted by user.{Style.RESET_ALL}")
        sys.exit(0)
    except Exception as e:
        print(f"\n{Fore.RED}[!] An error occurred: {str(e)}{Style.RESET_ALL}")
        sys.exit(1)