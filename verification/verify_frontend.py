from playwright.sync_api import sync_playwright

def verify_synth_frontend():
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()

        # Navigate to the hosted index.html
        # Since I started http.server on 8080, I can access it via localhost
        page.goto("http://localhost:8080/index.html")

        # Check title
        assert page.title() == "32fm Controller"

        # Check for Connect Button
        connect_btn = page.get_by_role("button", name="Connect to Synth")
        assert connect_btn.is_visible()

        # Check for Sliders (CC controls)
        # Filter Cutoff (CC 74)
        cutoff_slider = page.locator("input[data-cc='74']")
        assert cutoff_slider.is_visible()

        # Filter Resonance (CC 71)
        res_slider = page.locator("input[data-cc='71']")
        assert res_slider.is_visible()

        # FM Ratio (CC 20) -> Wait, I used CC 20 in C++ code, but did I update index.html?
        # Let's check index.html again.

        page.screenshot(path="verification/frontend_initial.png")
        print("Screenshot saved to verification/frontend_initial.png")

        browser.close()

if __name__ == "__main__":
    verify_synth_frontend()
