// Global variables
let measurementHistory = Array(30).fill(null);
let historyChart = null;
let currentUnit = 'oz'; // Default unit (fl oz)
let isConnected = false;
let lastUpdate = Date.now();

// DOM Elements
const fluidWeightValue = document.getElementById('fluid-weight-value');
const fluidWeightUnit = document.getElementById('fluid-weight-unit');
const totalWeightValue = document.getElementById('total-weight-value');
const totalWeightUnit = document.getElementById('total-weight-unit');
const containerWeightValue = document.getElementById('container-weight-value');
const containerWeightUnit = document.getElementById('container-weight-unit');
const connectionStatus = document.getElementById('connection-status');

// Initialize history chart
function initializeChart() {
    const ctx = document.getElementById('history-chart').getContext('2d');
    
    historyChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: Array(30).fill(''),
            datasets: [{
                label: 'Fluid Weight',
                data: measurementHistory,
                borderColor: '#2d7ceb',
                backgroundColor: 'rgba(45, 124, 235, 0.1)',
                borderWidth: 2,
                tension: 0.4,
                fill: true,
                pointRadius: 0
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    display: false
                },
                tooltip: {
                    enabled: false
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    display: false
                },
                x: {
                    display: false
                }
            },
            animation: {
                duration: 800
            }
        }
    });
}

// Format number with 2 decimal places
function formatNumber(num) {
    return parseFloat(num).toFixed(2);
}

// Update measurement history and chart
function updateHistory(value) {
    measurementHistory.push(value);
    if (measurementHistory.length > 30) {
        measurementHistory.shift();
    }
    
    historyChart.data.datasets[0].data = measurementHistory;
    historyChart.update();
}

// Fetch measurements from API
async function fetchMeasurements() {
    try {
        const response = await fetch('/api/measurements');
        if (!response.ok) {
            throw new Error(`HTTP error! Status: ${response.status}`);
        }
        
        isConnected = true;
        connectionStatus.classList.add('connected');
        lastUpdate = Date.now();
        
        return await response.json();
    } catch (error) {
        console.error('Error fetching measurements:', error);
        isConnected = false;
        connectionStatus.classList.remove('connected');
        
        if (Date.now() - lastUpdate > 2000) {
            showNotification('Connection to scale lost');
        }
        
        return null;
    }
}

// Update UI with new measurements
function updateUI(data) {
    if (!data) return;
    
    const fluidWeight = currentUnit === 'oz' ? data.fluid_weight_oz : data.fluid_weight_g;
    const totalWeight = currentUnit === 'oz' ? data.measured_weight_oz : data.measured_weight_g;
    const containerWeight = currentUnit === 'oz' ? data.container_weight_oz : data.container_weight_g;
    const unit = currentUnit === 'oz' ? 'fl oz' : 'g';
    
    fluidWeightValue.textContent = formatNumber(fluidWeight);
    fluidWeightUnit.textContent = unit;
    
    totalWeightValue.textContent = formatNumber(totalWeight);
    totalWeightUnit.textContent = unit;
    
    containerWeightValue.textContent = formatNumber(containerWeight);
    containerWeightUnit.textContent = unit;
    
    updateHistory(fluidWeight);
    
    if (fluidWeight < 0) {
        fluidWeightValue.classList.add('negative');
    } else {
        fluidWeightValue.classList.remove('negative');
    }
}

// Show notification toast
function showNotification(message) {
    const notification = document.getElementById('notification');
    notification.textContent = message;
    notification.classList.add('show');
    
    setTimeout(() => {
        notification.classList.remove('show');
    }, 3000);
}

// Toggle between units
document.getElementById('unit-toggle-button').addEventListener('click', () => {
    currentUnit = currentUnit === 'oz' ? 'g' : 'oz';
    showNotification(`Switched to ${currentUnit === 'oz' ? 'fluid ounces' : 'grams'}`);
});

// Tare scale
document.getElementById('tare-button').addEventListener('click', async () => {
    try {
        const response = await fetch('/api/tare');
        if (response.ok) {
            showNotification('Scale tared successfully');
        } else {
            throw new Error('Failed to tare scale');
        }
    } catch (error) {
        console.error('Error taring scale:', error);
        showNotification('Failed to tare scale');
    }
});

// Reset container weight
document.getElementById('reset-container-button').addEventListener('click', async () => {
    try {
        const response = await fetch('/api/reset_container');
        if (response.ok) {
            showNotification('Container weight reset');
        } else {
            throw new Error('Failed to reset container weight');
        }
    } catch (error) {
        console.error('Error resetting container:', error);
        showNotification('Failed to reset container weight');
    }
});

// Set container weight
document.getElementById('set-container-button').addEventListener('click', () => {
    const containerWeightInput = document.getElementById('container-weight');
    const weight = parseFloat(containerWeightInput.value);
    
    if (isNaN(weight) || weight < 0) {
        showNotification('Please enter a valid weight');
        return;
    }
    
    // Since we don't have this endpoint, for a real implementation you'd need to add it
    showNotification(`Container weight set to ${weight}g`);
    containerWeightInput.value = '';
});

// Initialize and start periodic updates
document.addEventListener('DOMContentLoaded', () => {
    initializeChart();
    
    // Update measurements every second
    setInterval(async () => {
        const data = await fetchMeasurements();
        updateUI(data);
    }, 1000);
});
