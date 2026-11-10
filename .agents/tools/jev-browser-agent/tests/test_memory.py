"""
Unit tests for sliding memory window and sub-goal management.
"""

from jev_browser_agent.memory import SlidingMemoryWindow, SubGoalManager
from jev_browser_agent.models import ActionDecision, ElementInfo


def test_sliding_memory_window_capped_at_five():
    window = SlidingMemoryWindow(max_size=5)

    el = ElementInfo(
        id=1,
        tag="button",
        role="button",
        text="Click Me",
        selector='[data-jev-id="1"]',
    )

    # Add 8 actions
    for i in range(1, 9):
        decision = ActionDecision(
            action_type="click",
            target_id=str(i),
            confidence_action=0.9,
            confidence_target=0.9,
            source="jev",
        )
        window.add_action(
            step=i,
            decision=decision,
            element=el,
            outcome=f"Clicked {i}",
            url_after=f"https://example.com/{i}",
        )

    # Check window size strictly capped at 5
    recent = window.get_recent_history()
    assert len(recent) == 5
    assert window.total_actions_count == 8

    # Ensure FIFO sliding: earliest is step 4, latest is step 8
    assert recent[0].step == 4
    assert recent[-1].step == 8

    # Formatted prompt output
    prompt_str = window.format_for_prompt()
    assert "Last 5 Actions" in prompt_str
    assert "Step 4" in prompt_str
    assert "Step 8" in prompt_str
    assert "Step 1" not in prompt_str


def test_loop_detection_repeated_actions():
    window = SlidingMemoryWindow(max_size=5)
    decision = ActionDecision(
        action_type="click",
        target_id="2",
        confidence_action=0.8,
        confidence_target=0.8,
        source="jev",
    )

    # 2 repeated actions: no loop
    window.add_action(1, decision, None, "Clicked 2", "url")
    window.add_action(2, decision, None, "Clicked 2", "url")
    looping, _ = window.detect_loop()
    assert not looping

    # 3 repeated actions: loop triggered
    window.add_action(3, decision, None, "Clicked 2", "url")
    looping, reason = window.detect_loop()
    assert looping
    assert "repeated 3 times" in reason


def test_sub_goal_manager():
    manager = SubGoalManager(
        overall_goal="Register an account",
        sub_goals=["1. Open register page", "2. Fill email and password", "3. Submit"],
    )

    assert manager.current_sub_goal == "1. Open register page"
    assert manager.advance_sub_goal() is True
    assert manager.current_sub_goal == "2. Fill email and password"
    assert manager.advance_sub_goal() is True
    assert manager.current_sub_goal == "3. Submit"
    assert manager.advance_sub_goal() is False  # No more sub-goals
    assert manager.current_sub_goal == "3. Submit"

