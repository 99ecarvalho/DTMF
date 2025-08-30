#!/usr/bin/env python3
"""
debug_youtube.py - Debug YouTube format issues

Simple script to test yt-dlp format availability and extraction.
"""

import sys
import subprocess

def test_formats(youtube_url, cookies_file=None):
    """Test different format combinations."""
    
    formats_to_try = [
        "bestaudio/best",
        "bestaudio",
        "best",
        "worst",
        "worstaudio",
        "139",  # Common YouTube audio format
        "140",  # Common YouTube audio format
        "251",  # webm audio
        "250",  # webm audio
    ]
    
    print(f"Testing formats for: {youtube_url}")
    print("=" * 60)
    
    for fmt in formats_to_try:
        print(f"\nTesting format: {fmt}")
        cmd = [
            'yt-dlp',
            '--format', fmt,
            '--get-url',
            '--no-warnings',
            youtube_url
        ]
        
        if cookies_file:
            cmd.extend(['--cookies', cookies_file])
            
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
            if result.returncode == 0 and result.stdout.strip():
                print(f"✓ SUCCESS: {fmt}")
                print(f"  URL: {result.stdout.strip()[:100]}...")
                return fmt, result.stdout.strip()
            else:
                print(f"✗ FAILED: {fmt}")
                if result.stderr:
                    print(f"  Error: {result.stderr[:200]}...")
        except subprocess.TimeoutExpired:
            print(f"⏱ TIMEOUT: {fmt}")
        except Exception as e:
            print(f"💥 EXCEPTION: {fmt} - {e}")
    
    return None, None

def list_all_formats(youtube_url, cookies_file=None):
    """List all available formats."""
    print("\nListing all available formats:")
    print("=" * 60)
    
    cmd = [
        'yt-dlp',
        '--list-formats',
        '--no-warnings',
        youtube_url
    ]
    
    if cookies_file:
        cmd.extend(['--cookies', cookies_file])
        
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=15)
        if result.returncode == 0:
            print(result.stdout)
        else:
            print(f"Failed to list formats: {result.stderr}")
    except subprocess.TimeoutExpired:
        print("Timeout while listing formats")
    except Exception as e:
        print(f"Exception while listing formats: {e}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 debug_youtube.py <youtube_url> [cookies.txt]")
        sys.exit(1)
    
    youtube_url = sys.argv[1]
    cookies_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    if cookies_file:
        print(f"Using cookies: {cookies_file}")
    
    # Test individual formats
    working_format, url = test_formats(youtube_url, cookies_file)
    
    # List all formats
    list_all_formats(youtube_url, cookies_file)
    
    if working_format:
        print(f"\n🎉 RECOMMENDATION: Use format '{working_format}'")
        print(f"Stream URL: {url}")
    else:
        print("\n❌ No working formats found")

if __name__ == "__main__":
    main()
