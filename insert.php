<?php

$data = json_decode(file_get_contents('php://input'), true);
$date = date("Y-m-d H:i:s");
$station = $data["station"];
$temperature = $data["temperature"];
$weight = $data["weight"];
$battery = $data["battery"];
$reading = $data["reading"];

$servername = "<SERVERNAME>";
$username = "<USERNAME>";
$password = "<PASSWORD>";
$dbname = "<DB_NAME>";

$conn = new mysqli($servername, $username, $password, $dbname);
if ($conn->connect_error) {
  die("Connection failed: " . $conn->connect_error);
}

$insert = "INSERT INTO `beehive` (`station`, `datetime`, `temperature`, `weight`, `battery`, `reading`) VALUES ('" . $station . "', '" . $date . "', '" . $temperature . "', '" . $weight . "', '" . $battery . "', '" . $reading . "');";
$update = "INSERT INTO `lastupdate` (`station`, `datetime`) VALUES('" . $station . "', '" . $date . "') ON DUPLICATE KEY UPDATE `datetime`='" . $date . "';";

if (isset($data) && $conn->query($insert) === TRUE && $conn->query($update) === TRUE) {
  echo $date . " => new record created successfully";
} else {
  echo $date . " => error: " . $sql . " => " . $conn->error;
}

$conn->close();

?>