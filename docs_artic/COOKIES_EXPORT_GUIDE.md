# How to Export YouTube Cookies for DTMF Analysis

## Why Use Cookies?
YouTube's bot detection can block automated access. Using cookies from your logged-in browser session bypasses this restriction.

## Method 1: Browser Extension (Recommended)

### Chrome/Edge/Chromium:
1. Install "Get cookies.txt LOCALLY" extension from Chrome Web Store
2. Go to YouTube.com while logged in (make sure you can access videos normally)
3. Click the extension icon in your browser toolbar
4. Click "Export" → Select "youtube.com" → Click "Download"
5. Save as `cookies.txt` in your DTMF directory
6. **Important**: Make sure the file contains lines starting with `.youtube.com` and has session tokens

### Firefox:
1. Install "cookies.txt" extension
2. Go to YouTube.com while logged in  
3. Click the extension icon
4. Select "Current Site" or "All Cookies"
5. Save as `cookies.txt`

## Method 2: Manual Export (Chrome DevTools)

1. Open Chrome DevTools (F12)
2. Go to Application → Storage → Cookies → https://youtube.com
3. Right-click in the cookies list
4. Select "Copy" → "Copy All as cURL" (or use a cookie export script)
5. Convert to Netscape format

## Method 3: Command Line (Linux/Mac)

### If you use Firefox:
```bash
# Firefox cookies are in ~/.mozilla/firefox/*/cookies.sqlite
# Use a tool to convert SQLite to Netscape format
```

### If you use Chrome:
```bash
# Chrome cookies are in ~/.config/google-chrome/Default/Cookies
# Use a tool to extract and convert
```

## Using the Cookies

Once you have `cookies.txt`, use it with the YouTube streaming script:

```bash
# Basic usage with cookies
python3 youtube_stream.py --cookies cookies.txt "https://www.youtube.com/watch?v=VIDEO_ID" | ./build/dtmf_stream

# Using the Makefile (need to add support)
make test-youtube-cookies YOUTUBE_URL='VIDEO_URL' COOKIES_FILE='cookies.txt'
```

## Cookie File Format

The cookies.txt file should be in Netscape format:
```
# Netscape HTTP Cookie File
.youtube.com	TRUE	/	FALSE	1234567890	session_token	ABC123...
.youtube.com	TRUE	/	TRUE	1234567890	VISITOR_INFO1_LIVE	xyz789...
```

## Security Notes

⚠️ **Important Security Considerations:**
- Cookies contain authentication tokens
- Don't share cookie files with others
- Don't commit cookie files to version control
- Regenerate/delete cookies periodically
- Use temporary/burner accounts for testing

## Troubleshooting

**"Cookies file not found" error:**
- Check the file path is correct
- Ensure the file exists and is readable

**Still getting bot detection:**
- Cookies may be expired - re-export them immediately after testing in browser
- Make sure you can access the specific video in your browser before exporting cookies
- Try logging out and back into YouTube, then re-export cookies
- Some videos may still be restricted regardless of authentication
- The cookies file might be corrupted - try re-exporting

**Cookie validation:**
- Check that cookies.txt contains `.youtube.com` entries
- Verify the file isn't empty: `wc -l cookies.txt` should show multiple lines
- Check file format: Should be Netscape format with tab-separated values
- Test cookies work: Try accessing YouTube in a private/incognito window with the same account

**Permission issues:**
- Ensure the cookies.txt file has proper read permissions: `chmod 644 cookies.txt`
- Check that the file isn't corrupted or empty
- Make sure the file path is correct relative to where you run the script

**Alternative if cookies fail:**
If cookies continue to fail, use the download-first approach:
```bash
yt-dlp -x --audio-format wav "https://www.youtube.com/watch?v=VIDEO_ID"
ffmpeg -i "downloaded_file.wav" -f s16le -ac 1 -ar 8000 converted.pcm
./build/dtmf_stream converted.pcm
```
