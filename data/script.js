document.addEventListener("DOMContentLoaded", function() {
  const ssidSelect = document.getElementById("ssid");
  const spinner = document.getElementById("ssid-spinner");

  const currentSsid = ssidSelect.getAttribute("current-data");

  ssidSelect.classList.add("has-spinner");

  fetch("/scan")
    .then(res => res.json())
    .then(data => {
      ssidSelect.innerHTML = "";
      data.forEach(ssid => {
        const option = document.createElement("option");
        option.value = ssid;
        option.textContent = ssid;

        if (ssid === currentSsid) {
          option.selected = true;
        }

        ssidSelect.appendChild(option);
      });
      ssidSelect.disabled = false;
      spinner.classList.add("hidden");
      ssidSelect.classList.remove("has-spinner");
    })
    .catch(err => {
      ssidSelect.innerHTML = "<option>Error scanning Wi-Fi</option>";
      spinner.classList.add("hidden");
      ssidSelect.classList.remove("has-spinner");
      console.error(err);
    });
});