document.addEventListener("DOMContentLoaded", function() {
    const progressFill = document.getElementById("progress-fill");
    const countdownEl = document.getElementById("countdown");

    // Countdown and progress
    let seconds = 3;
    const totalTime = 3000; // 3 seconds
    const interval = 100; // update every 100ms

    const timer = setInterval(function() {
        const remaining = seconds * 1000;
        const elapsed = totalTime - remaining;
        const progress = (elapsed / totalTime) * 100;

        progressFill.style.width = progress + "%";
        countdownEl.textContent = `Rebooting in ${Math.floor(seconds)} seconds...`;

        if (seconds <= 0) {
            clearInterval(timer);
            countdownEl.textContent = "Rebooting...";
            progressFill.style.width = "100%";
        }

        seconds -= interval / 1000;
    }, interval);
});
